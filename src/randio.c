/*
 * Randio music player
 * Copyright (C) Eskild Hustvedt 2011, 2012, 2017
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <gst/gst.h>

#include <rest-extras/lastfm-proxy.h>

#include <sqlite3.h>

#include <stdbool.h>
#include <time.h>
#include <sys/types.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>

#include "randio-datatypes.h"
#include "randio-prefs.h"
#include "randio-lastfm.h"
#include "randio-sql.h"
#include "randio.h"

/* Global widgets */
GtkWidget *playingTrackLabel;
GtkWidget *playingAlbumLabel;
GtkWidget *playStopButton;
GtkWidget *timeWidget;
/* The GStreamer pipeline */
GstElement *pipeline;
/* Structure tracking track info. The struct i defined in randio-datatypes.h */
struct trackTag currTrack;
/* Our global state, contains the main window, the GtkApplication and our GtkBuilder */
struct randioGlobalStateStruct randioGlobalState;

bool playOnlyLoved = false;

/* This is a boolean that defines if we believe that we are playing or not.
 * In almost all cases you should be galling gst_element_get_state() instead,
 * but this can be used in functions that are called very frequently, such as tick()
 * to get a mostly-correct idea about the state
 */
bool statePlaying = false;
/* This is a global notification variable. This is kept so that we can
 * notify_notification_close() it before displaying a new one */
GNotification *notification = NULL;

/*
 * ******************
 * Playback functions
 * ******************
 */

/*
 * Play a track, identified by the track id number supplied
 */
bool playTrack (int trackID)
{
  char SQL[254];
  sqlite3_stmt *statement;
  const char *track;
  char *path;

  sprintf(SQL,"SELECT path FROM tracks WHERE track_id=%d",trackID);
  sqlite3_prepare_v2(db, SQL, -1, &statement, NULL);
  sqlite3_step(statement);
  track = strdup((char*) sqlite3_column_text(statement,0));
  sqlite3_finalize(statement);

  path = malloc(7+strlen(track)+1);
  sprintf(path,"file://%s",track);
  playFile(path);

  // Get the lock so we can write to currTrack
  g_mutex_lock(&currTrack.lock);

  currTrack.trackID = trackID;
  currTrack.startedPlaying = time(NULL);
  if(currTrack.currTrackPath)
  {
    free(currTrack.currTrackPath);
  }
  currTrack.currTrackPath = path;

  g_mutex_unlock(&currTrack.lock);

  /*
   * Yes, by this time gstreamer has probably already errored out,
   * but stats can be quite slow (ie. over nfs), so performing the check
   * afterwards gives a better interactive feel most of the time.
   *
   * The result is that tracks start playing sooner as long as the
   * track exists, if it doesn't then we handle it here.
   */
  if (g_access(track,R_OK) != 0)
    return false;

  sprintf(SQL, "INSERT INTO played (track_id) VALUES (%d)",trackID);
  SQL_exec(SQL);

  return true;
}

/*
 * Start playback of a file. Expects a fully qualified path (ie. with file://)
 */
void playFile (char *file)
{
  clearCurrent();
  // Reset the state to null (stops any current playback)
  gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);
  // Set the file path
  g_object_set(G_OBJECT(pipeline), "uri", file, NULL);
  // Run the main iteration to update the UI
  gtkMainIteration();
  // Start playing
  gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);
}

void handleTagMessage (GstTagList *tags)
{
  const char* artistTags[] = {GST_TAG_ARTIST, GST_TAG_ALBUM_ARTIST, GST_TAG_PERFORMER, GST_TAG_COMPOSER};
  gchar *content;
  bool foundArtist = false;
  bool foundTrack = false;
  // Lock currTrack
  g_mutex_lock(&currTrack.lock);

  /*
   * Get the artist. We permit this value to be fetched from multiple locations, so
   * we loop over artistTags to see if we can find one
   */
  for(int i = 0; i < (sizeof(artistTags) / sizeof(const char*)); i++)
  {
    if (gst_tag_list_get_string(tags,artistTags[i],&content))
    {
      strcpy(currTrack.trackArtist,content);
      foundArtist = true;
      break;
    }
  }

  if (!foundArtist)
  {
    strcpy(currTrack.trackArtist,"Unknown artist");
  }

  if (gst_tag_list_get_string(tags,GST_TAG_TITLE,&content))
  {
    strcpy(currTrack.trackName,content);
    foundTrack = true;
  }
  else
  {
    if (gst_tag_list_get_string(tags,GST_TAG_LOCATION,&content))
    {
      strcpy(currTrack.trackName,content);
    }
    else
    {
      strcpy(currTrack.trackName,currTrack.currTrackPath);
    }
  }

  if (gst_tag_list_get_string(tags,GST_TAG_ALBUM,&content))
  {
    currTrack.hasAlbum = true;
    strcpy(currTrack.trackAlbum,content);
  }
  else
  {
    strcpy(currTrack.trackAlbum,"Unknown album");
  }

  currTrack.hasBasicInfo = (foundTrack && foundArtist);

  // Unlock currTrack
  g_mutex_unlock(&currTrack.lock);
  // Display notification if needed
  if(currTrack.hasAlbum)
  {
    displayTrackNotification();
  }
  // Update the label
  updateLabel();
}

/*
 * This will display our desktop notification if we haven't already, and we
 * have got the basic track info
 */
void displayTrackNotification (void)
{
  char *summary;
  char *body = NULL;

  if(!currTrack.hasBasicInfo || currTrack.notificationDisplayed)
  {
    return;
  }

  /*
   * There's a tiny chance that notificationDisplayed has been changed before
   * we get the mutex, it's so unlikely that we won't bother checking though
   * (and it is pretty much harmless anyway)
   */
  if(g_mutex_trylock(&currTrack.lock) == FALSE)
  {
    return;
  }

  /*
   * Generate the summary string, artist - track
   */
  summary = malloc(strlen(currTrack.trackArtist)+strlen(currTrack.trackName)+4);
  sprintf(summary,"%s - %s",currTrack.trackArtist,currTrack.trackName);

  /*
   * Hide any current notification we've got
   */
  if (notification == NULL)
  {
    notification = g_notification_new(summary);
    g_notification_set_priority(notification,G_NOTIFICATION_PRIORITY_HIGH);
  }
  else
  {
    g_notification_set_title(notification,summary);
  }

  /*
   * If we've got an album use that as the body
   */
  if(currTrack.hasAlbum)
  {
    body = malloc(strlen(currTrack.trackAlbum)+6);
    sprintf(body,"from %s",currTrack.trackAlbum);
    g_notification_set_body(notification,"");
  }
  else
  {
    g_notification_set_body(notification,body);
  }

  /*
   * Display the notification
   */
  g_application_send_notification(G_APPLICATION(randioGlobalState.app),"randio-playing",notification);

  free(summary);
  if(body != NULL)
  {
    free(body);
  }

  currTrack.notificationDisplayed = true;

  g_mutex_unlock(&currTrack.lock);
}

/*
 * Handle messages from gstreamer
 */
static gboolean gstMessage (GstBus *bus, GstMessage *msg, gpointer user_data)
{
  GstTagList *tags = NULL;
  GError *err = NULL;
  gchar *dbg_info = NULL;
  gchar *file = NULL;
  switch (GST_MESSAGE_TYPE(msg))
  {
    case GST_MESSAGE_EOS:
      // Run the main iteration to update the UI
      gtkMainIteration();

      lastfmSubmitTrack(currTrack);
      nextTrack();
      break;
    case GST_MESSAGE_ERROR:
      g_object_get(G_OBJECT(pipeline),"uri",&file,NULL);

      gst_message_parse_error (msg, &err, &dbg_info);
      g_printerr ("ERROR from element %s while playing \"%s\": %s\n", GST_OBJECT_NAME (msg->src), file, err->message);
      g_error_free (err);
      g_free (dbg_info);
      break;
    case GST_MESSAGE_TAG:
      gst_message_parse_tag (msg, &tags);
      handleTagMessage(tags);
      gst_tag_list_free(tags);
      break;
    case GST_MESSAGE_STATE_CHANGED:
      updateWinStateInfo();
      break;
    case GST_MESSAGE_DURATION:
      setTrackDuration();
      break;
    default:
      break;
  }

  return TRUE;
}

/*
 * Toggle the "play" state (play/pause)
 */
void togglePlaying (void)
{
  GstState state;
  GtkWidget *nextButton;
  GtkWidget *loveButton;
  GtkWidget *banButton;

  gst_element_get_state(GST_ELEMENT(pipeline), &state, NULL,GST_CLOCK_TIME_NONE);
  if(state == GST_STATE_PLAYING)
  {
    gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PAUSED);
  }
  else if (state == GST_STATE_PAUSED)
  {
    gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);
  }
  // No state, ie. we haven't started playing
  else
  {
    nextButton = GTK_WIDGET(gtk_builder_get_object(randioGlobalState.uiBuilder,"nextButton"));
    loveButton = GTK_WIDGET(gtk_builder_get_object(randioGlobalState.uiBuilder,"loveButton"));
    banButton = GTK_WIDGET(gtk_builder_get_object(randioGlobalState.uiBuilder,"banButton"));
    // Update the label of the play button immediately in order to make the
    // UI feel responsive to the users actions
    gtk_stack_set_visible_child_name(GTK_STACK(playStopButton),"playButtonStatePlaying");
    // Make the state buttons sensitive. This is only done once during our
    // runtime
    gtk_widget_set_sensitive(nextButton,true);
    gtk_widget_set_sensitive(loveButton,true);
    gtk_widget_set_sensitive(banButton,true);
    nextTrack();
  }
}

/*
 * Runs nextTrack in a thread
 */
void nextTrack (void)
{
  g_thread_new("nextTrack", (GThreadFunc) nextTrackInThread,NULL);
}

/*
 * Skip to the next track
 */
void nextTrackInThread (void)
{
  for(int attempt = 0; attempt < 10; attempt++)
  {
    int trackID = SQL_getTrackRND(playOnlyLoved,currTrack);
    if(trackID == -1)
    {
      // Delete all already played tracks
      SQL_exec("DELETE FROM played");
      trackID = SQL_getTrackRND(playOnlyLoved,currTrack);
      // FIXME: Should tell the user
      if(trackID == -1)
      {
        printf("No tracks found in database\n");
        return;
      }
    }
    if (playTrack(trackID))
      return;
  }
}

/*
 * Ban the current track
 */
void banTrack (void)
{
  if(currTrack.trackID == -1)
    return;
  sqlite3_stmt *statement;

  // Insert into our banned table
  sqlite3_prepare_v2(db,"UPDATE tracks SET banned=1 WHERE track_id=?",-1,&statement, NULL);
  sqlite3_bind_int(statement, 1, currTrack.trackID);
  sqlite3_step(statement);
  sqlite3_finalize(statement);

  // Then drop from loved (if it exists) and tracks
  sqlite3_prepare_v2(db,"DELETE FROM loved WHERE track_id=?",-1,&statement, NULL);
  sqlite3_bind_int(statement, 1, currTrack.trackID);
  sqlite3_step(statement);
  sqlite3_finalize(statement);

  // Finally skip to the next track
  nextTrack();
}

/*
 * Love the current track
 */
void loveTrack (void)
{
  if(currTrack.trackID == -1)
    return;
  char SQL[254];
  sprintf(SQL,"INSERT INTO loved (track_id) VALUES (%d)",currTrack.trackID);
  SQL_exec(SQL);
}

/*
 * Initialize gstreamer
 */
void initGST (void)
{
  pipeline    = gst_element_factory_make("playbin", "player");
  GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
  gst_bus_add_watch(bus, gstMessage, NULL);
  gst_object_unref(bus);
}

/*
 * Handle media key events
 */
void handleMediaKeyEvent (GDBusProxy *proxy, gchar *sender_name, gchar *signal_name, GVariant *parameters, gpointer user_data)
{
	GVariantIter iter;
	GVariant *appName;
  GVariant *keyPress;

  if(strcmp(signal_name,"MediaPlayerKeyPressed") != 0)
  {
    return;
  }

  // Parameters is a GVariant containing (at least) two string values.
  // The first is the name of the app the message is intended for. We identify ourselves
  // as "randio". The second is the button that was pressed

  // We need the iter to fetch individual values from the tuple
	g_variant_iter_init (&iter, parameters);
  // Get the first one
  appName = g_variant_iter_next_value(&iter);
  // It can be NULL if there's no value. If so we silently fail
  if(appName == NULL)
  {
    g_variant_unref(appName);
    return;
  }
  // If the message isn't meant for us, we ignore it
  if (strcmp(g_variant_get_string(appName,NULL),"randio") != 0)
  {
    g_variant_unref(appName);
    return;
  }
  g_variant_unref(appName);

  // Fetch the next value, which is the key that was pressed
  keyPress = g_variant_iter_next_value(&iter);
  // It can also (in theory) be NULL, if it is we just silently return
  if(keyPress == NULL)
  {
    g_variant_unref(keyPress);
    return;
  }
  // Fetch the actual value and free the GVariant since we no longer need it
  const char *keyPressStr = g_variant_get_string(keyPress,NULL);
  g_variant_unref(keyPress);

  // Handle the various keys
  //
  // Play is handled as a toggle, as is Pause
  if(strcmp(keyPressStr,"Play") == 0 || strcmp(keyPressStr,"Pause") == 0)
  {
    togglePlaying();
  }
  // We handle stop as "pause" if we're currently playing. If we're already stopped
  // we just ignore it
  else if (strcmp(keyPressStr,"Stop") == 0)
  {
    GstState state;

    gst_element_get_state(GST_ELEMENT(pipeline), &state, NULL,GST_CLOCK_TIME_NONE);
    if(state == GST_STATE_PLAYING)
    {
      togglePlaying();
    }
  }
  // Handle next
  else if(strcmp(keyPressStr,"Next") == 0)
  {
    nextTrack();
  }
  // We ignore the following keys: Previous, Rewind, FastForward, Repeat, Shuffle
  //
  // If the key is none of those then it is a previously unknown key, so we output
  // a message about it.
  else if(
      strcmp(keyPressStr,"Previous")    != 0 &&
      strcmp(keyPressStr,"Rewind")      != 0 &&
      strcmp(keyPressStr,"FastForward") != 0 &&
      strcmp(keyPressStr,"Repeat")      != 0 &&
      strcmp(keyPressStr,"Shuffle")     != 0
      )
  {
    printf(" randio: unhandled media key: %s\n",keyPressStr);
  }
}

/*
 * Initialize media key events
 *
 * We use the gnome-settings-daemon media-key API through Dbus
 */
void initMediaKeys (void)
{
  GDBusProxy *mediaKeyBus = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SESSION,
      G_DBUS_PROXY_FLAGS_NONE,
      NULL,
      "org.gnome.SettingsDaemon",
      "/org/gnome/SettingsDaemon/MediaKeys",
      "org.gnome.SettingsDaemon.MediaKeys",
      NULL,
      NULL
      );
  g_dbus_proxy_call_sync (mediaKeyBus,
      "GrabMediaPlayerKeys",
      g_variant_new ("(su)",
        "randio",
        0),
      G_DBUS_CALL_FLAGS_NONE,
      -1,
      NULL,
      NULL);
  g_signal_connect(mediaKeyBus,"g-signal", G_CALLBACK(handleMediaKeyEvent), NULL);
}

/*
 * Get the current track duration in seconds
 */
int trackDuration (void)
{
    gint64 duration;
    GstFormat fmt = GST_FORMAT_TIME;

    gst_element_query_duration(pipeline, fmt, &duration);
    duration = duration / 1000000000;
    return (int) duration;
}

/*
 * Get the current track position in seconds
 */
int trackPosition (void)
{
    gint64 duration;
    GstFormat fmt = GST_FORMAT_TIME;

    gst_element_query_position(pipeline, fmt, &duration);
    duration = duration / 1000000000;
    return (int) duration;
}

/*
 * ************
 * UI functions
 * ************
 */

/*
 * Run through the gtk main iteration, processing any events (refreshes the
 * UI). Most places this is used, a separate thread should be used instead.
 */
void gtkMainIteration (void)
{
  while (gtk_events_pending ())
    gtk_main_iteration ();
}

/*
 * Construct the main window UI
 */
void initUI (void)
{
  GMenuModel *app_menu;
  GtkBuilder *menuBuilder;
  randioGlobalState.uiBuilder = gtk_builder_new_from_resource("/org/zerodogg/randio/randio.ui");
  gtk_builder_add_callback_symbol(randioGlobalState.uiBuilder,"togglePlaying",G_CALLBACK(togglePlaying));
  gtk_builder_add_callback_symbol(randioGlobalState.uiBuilder,"nextTrack",G_CALLBACK(nextTrack));
  gtk_builder_add_callback_symbol(randioGlobalState.uiBuilder,"banTrack",G_CALLBACK(banTrack));
  gtk_builder_add_callback_symbol(randioGlobalState.uiBuilder,"loveTrack",G_CALLBACK(loveTrack));
  gtk_builder_connect_signals(randioGlobalState.uiBuilder,NULL);
  randioGlobalState.mainWindow = GTK_WIDGET(gtk_builder_get_object(randioGlobalState.uiBuilder,"mainWindow"));

  playStopButton = GTK_WIDGET(gtk_builder_get_object(randioGlobalState.uiBuilder,"playPauseButton"));
  timeWidget = GTK_WIDGET(gtk_builder_get_object(randioGlobalState.uiBuilder,"timer"));
  playingTrackLabel = GTK_WIDGET(gtk_builder_get_object(randioGlobalState.uiBuilder,"trackname"));
  playingAlbumLabel = GTK_WIDGET(gtk_builder_get_object(randioGlobalState.uiBuilder,"albumname"));

  menuBuilder = gtk_builder_new_from_resource("/org/zerodogg/randio/gtk/menus.ui");
  app_menu = G_MENU_MODEL (gtk_builder_get_object (menuBuilder, "app-menu"));
  gtk_application_add_window(randioGlobalState.app,GTK_WINDOW(randioGlobalState.mainWindow));
  gtk_application_set_app_menu (GTK_APPLICATION (randioGlobalState.app), app_menu);
  g_object_unref(menuBuilder);
}

/*
 * Update the track information label
 */
void updateLabel (void)
{
  // FIXME
  char trackLabel[254];
  char albumLabel[254];
  if(currTrack.trackName[0] != 0 && currTrack.trackArtist[0] != 0)
  {
    sprintf(trackLabel, "%s - %s", currTrack.trackArtist, currTrack.trackName);
    sprintf(albumLabel, "from %s", currTrack.trackAlbum);
  }
  else
  {
    sprintf(trackLabel, "(unknown)");
    sprintf(albumLabel, "(unknown)");
  }
  gtk_label_set_text(GTK_LABEL(playingTrackLabel),trackLabel);
  gtk_label_set_text(GTK_LABEL(playingAlbumLabel),albumLabel);

  // Force a tick to update the time label too
  tick();

  // Run the main iteration to update the UI
  gtkMainIteration();
}

/*
 * Update the state information for the window, including labels and buttons
 */
void updateWinStateInfo (void)
{
  GstState state;
  gst_element_get_state(GST_ELEMENT(pipeline), &state, NULL,GST_CLOCK_TIME_NONE);
  switch(state)
  {
    case GST_STATE_PLAYING:
      gtk_stack_set_visible_child_name(GTK_STACK(playStopButton),"playButtonStatePlaying");
      statePlaying = true;
      break;
    case GST_STATE_PAUSED:
      gtk_stack_set_visible_child_name(GTK_STACK(playStopButton),"playButtonStateStopped");
      statePlaying = false;
      break;
    default:
      break;
  }

  // Run the main iteration to update the UI
  gtkMainIteration();
}

/*
 * Display the about box
 */
void showAboutBox (void)
{
  gchar const *authors[] = {
    "Eskild Hustvedt https://www.zerodogg.org/",
    NULL};
  gtk_show_about_dialog (GTK_WINDOW(randioGlobalState.mainWindow),
      "program-name", "Randio",
      "title","About Randio",
      "license-type",GTK_LICENSE_GPL_3_0,
      "copyright","© Eskild Hustvedt 2017",
      "authors",authors,
      NULL);
}

/*
 * ******************
 * Helper++ functions
 * ******************
 */

/*
 * Detect and, if needed, create our config dir
 */
char* getConfDir (void)
{
  const char *confLoc = g_get_user_config_dir();
  char *confDir       = malloc(strlen(confLoc)+8);
  sprintf(confDir,"%s/randio",confLoc);
  if(g_mkdir_with_parents(confDir, 0750) == -1)
  {
    printf("Failed to create confDir (%d) - bailing out\n",errno);
    destroyApp(NULL,NULL);
  }
  return confDir;
}

/*
 * Sets the "duration" of the current track in our metadata
 */
void setTrackDuration (void)
{
  int minutes;
  int seconds;
  int tpos = trackDuration();
  minutes = tpos/60;
  seconds = tpos-(minutes*60);
  // FIXME: Should lock the mutex
  sprintf(currTrack.trackLength, "%02d:%02d", minutes, seconds);
  if(currTrack.trackLenSeconds == 0)
  {
    currTrack.trackLenSeconds = tpos;
  }
}

/*
 * A tick, runs on average once every 0.5s
 */
gboolean tick (void)
{
  if (statePlaying == false)
  {
    return TRUE;
  }
  int tpos    = trackPosition();
  int minutes = tpos/60;
  int seconds = tpos-(minutes*60);
  char currPos[10];
  char label[25];
  sprintf(currPos, "%02d:%02d", minutes, seconds);

  /*
   * Submit "now playing" to last.fm after 3 seconds
   */
  if(tpos > 3 && !currTrack.submittedNowPlaying)
  {
    // Run the main iteration to update the UI
    gtkMainIteration();

    lastfmSubmitCurrentlyPlaying(currTrack);
  }
  /*
   * Make sure that we have displayed our notification within
   * a reasonable time (it is slightly delayed while waiting for
   * GStreamer to read the file and hand us the tags (but if
   * a track doesn't have an artist, it might end up not displaying
   * at all)
   */
  if(tpos > 0 && !currTrack.notificationDisplayed)
  {
    displayTrackNotification();
  }

  if(currTrack.trackLength[0] == 0)
  {
    setTrackDuration();
  }

  sprintf(label,"%s/%s",currPos,currTrack.trackLength);
  gtk_label_set_text(GTK_LABEL(timeWidget),label);
  return TRUE;
}

/*
 * Reset the currTrack struct
 */
void clearCurrent (void)
{
  currTrack.trackName[0] = 0;
  currTrack.trackArtist[0] = 0;
  currTrack.submittedNowPlaying = false;
  currTrack.scrobbled = false;
  currTrack.startedPlaying = 0;
  currTrack.trackLength[0] = 0;
  currTrack.trackLenSeconds = 0;
  currTrack.hasBasicInfo = false;
  currTrack.hasAlbum = false;
  currTrack.notificationDisplayed = false;
  currTrack.trackID = -1;
  sprintf(currTrack.trackAlbum,"(unknown album)");
}

/*
 * Close the application
 */
static void closeApp (void)
{
  gtk_widget_destroy(GTK_WIDGET(randioGlobalState.mainWindow));
}

/*
 * Cleanly terminate
 */
static void destroyApp (GtkWidget *widget, gpointer data)
{
  sqlite3_close(db);
}

/*
 * A wrapper around GSimpleAction that will create an action, set up funcPtr as
 * the callback to the activate signal and add the action to the GtkApplication
 * action map.
 */
void buildGAction (const char *name, void *funcPtr)
{
  GSimpleAction *action;
  action = g_simple_action_new(name,NULL);
  g_signal_connect(action,"activate",G_CALLBACK(funcPtr),&randioGlobalState);
  g_action_map_add_action(G_ACTION_MAP(randioGlobalState.app),G_ACTION(action));
}

/*
 * Initialize the app
 */
void app_init (GApplication *appRef, gpointer user_data)
{
  /*
   * Initialize the threading system
   */
  g_mutex_init(&currTrack.lock);
  /*
   * Initialize
   */
  initUI();
  SQLite_init( getConfDir() );
  initGST();
  lastfmInit();
  initMediaKeys();
  // Initializes currTrack to default values
  clearCurrent();
  /*
   * Set our tick function, runs once every 0.5s
   */
  g_timeout_add( (guint) 500, (GSourceFunc) tick, NULL);

  // Set up action handlers
  buildGAction("prefs",&showPrefs);
  buildGAction("quit",&closeApp);
  buildGAction("showAboutBox",&showAboutBox);
  buildGAction("loveTrack",&loveTrack);
  buildGAction("nextTrack",&nextTrack);
}

/*
 * "Activate" the app (aka. show the main window)
 */
void app_activate (GApplication *appRef, gpointer user_data)
{
  /* Show everything */
  gtk_widget_show_all(randioGlobalState.mainWindow);
}

/*
 * Main function, initialization then rest in the main loop
 */
int main (int argc, char *argv[])
{
  int status;

  randioGlobalState.app = gtk_application_new ("org.zerodogg.randio", G_APPLICATION_FLAGS_NONE);
  g_application_add_option_group (G_APPLICATION (randioGlobalState.app), gst_init_get_option_group ());
  g_signal_connect (randioGlobalState.app, "startup", G_CALLBACK (app_init), NULL);
  g_signal_connect (randioGlobalState.app, "activate", G_CALLBACK (app_activate), NULL);
  g_signal_connect (randioGlobalState.app, "shutdown", G_CALLBACK (destroyApp), NULL);
  status = g_application_run (G_APPLICATION (randioGlobalState.app), argc, argv);
  // FIXME: We're exiting, why bother unref-ing?
  g_object_unref (randioGlobalState.app);

  return status;
}
