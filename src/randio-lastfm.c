/*
 * Randio music player
 * last.fm support
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <glib.h>
#include <gtk/gtk.h>

#include <sqlite3.h>

#include <rest/rest-xml-parser.h>
#include <rest-extras/lastfm-proxy.h>

#include "randio-datatypes.h"
#include "randio-sql.h"
#include "randio-lastfm.h"

bool lastfmEnabled = false;
RestProxy *lastfmConnectProxy;

void lastfmRestCB (RestProxyCall *call, const GError *error)
{
  if(error != NULL)
  {
    const char* payload = rest_proxy_call_get_payload(call);
    printf("payload=%s\n",payload);
    printf("Last.fm request error: %s\n",error->message);
  }
}

/*
 * Return the content of a node in the XML, or NULL on error
 *
 * Note: The call parameter will be g_object_unref()ed
 */
char *getNodeContentFromREST (RestProxyCall *call, const char *node)
{
  GError *error         = NULL;
  RestXmlParser *parser = rest_xml_parser_new ();
  RestXmlNode *root     = rest_xml_parser_parse_from_data(parser, rest_proxy_call_get_payload(call), rest_proxy_call_get_payload_length(call));
  char *content;

  if (!lastfm_proxy_is_successful(root, &error))
  {
    printf("Last.fm REST proxy request: %s\n", error->message);
    g_object_unref(call);
    return NULL;
  }

  g_object_unref(parser);

  content = strdup(rest_xml_node_find(root, node)->content);
  rest_xml_node_unref(root);

  return content;
}

/*
 * Authenticate with last.fm (runs once, for the user to authorize us to access
 * the account)
 */
void lastfmConnect (GtkButton *button, GtkWindow *prefsWin)
{
  RestProxyCall *call;
  char *token;
  GError *error = NULL;
  const char *url;
  GtkWidget *dialog;

  lastfmConnectProxy = lastfm_proxy_new(LASTFM_APIKEY, LASTFM_SECRET);
  call               = rest_proxy_new_call(lastfmConnectProxy);

  rest_proxy_call_set_function(call, "auth.getToken");
  if (!rest_proxy_call_sync (call, NULL))
    g_error ("Cannot get token");

  token = getNodeContentFromREST(call,"token");
  g_object_unref(call);
  if(token == NULL)
  {
    // FIXME
    return;
  }
  url = lastfm_proxy_build_login_url(LASTFM_PROXY (lastfmConnectProxy), token);
  gtk_show_uri_on_window(GTK_WINDOW(prefsWin),
      url,
      GDK_CURRENT_TIME,
      &error);
  printf("%s\n",url);

  dialog = gtk_message_dialog_new(prefsWin, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "A browser window where you can allow randio to access your last.fm account has been opened. Once you have allowed it, click OK to continue.");
  gtk_dialog_run((GtkDialog*) dialog);
  gtk_widget_destroy(dialog);

  call = rest_proxy_new_call(lastfmConnectProxy);
  rest_proxy_call_set_function(call, "auth.getSession");
  rest_proxy_call_add_param(call, "token", token);

  if (!rest_proxy_call_sync (call, NULL))
  {
    dialog = gtk_message_dialog_new(prefsWin, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Failed to retrieve session information from last.fm, please try again later");
    gtk_dialog_run((GtkDialog*) dialog);
    gtk_widget_destroy(dialog);
  }
  else
  {
    const char *key = getNodeContentFromREST(call, "key");
    const char *name = getNodeContentFromREST(call,"name");
    SQL_setSetting("lastfmSession",key);
    SQL_setSetting("lastfmUser",name);
  }
  g_object_unref(call);
}

/*
 * Initialize last.fm if it is enabled
 */
void lastfmInit (void)
{
  unsigned char *session = SQL_getSetting("lastfmSession");
  if(session != NULL)
  {
    lastfmConnectProxy = lastfm_proxy_new(LASTFM_APIKEY, LASTFM_SECRET);
    lastfm_proxy_set_session_key (LASTFM_PROXY (lastfmConnectProxy), (char*) session);
    lastfmEnabled = true;
    free(session);
  }
}

/*
 * Submit "currently playing" to last.fm
 */
void lastfmSubmitCurrentlyPlaying (struct trackTag currTrack)
{
  RestProxyCall *call;

  if(currTrack.submittedNowPlaying || !lastfmEnabled)
  {
    return;
  }
  if(currTrack.trackName[0] != 0 && currTrack.trackArtist[0] != 0)
  {
    call = rest_proxy_new_call(lastfmConnectProxy);
    rest_proxy_call_set_function(call,"track.updateNowPlaying");
    rest_proxy_call_add_param(call,"track",currTrack.trackName);
    rest_proxy_call_add_param(call,"artist",currTrack.trackArtist);
    rest_proxy_call_set_method(call,"POST");
    rest_proxy_call_async(call,(RestProxyCallAsyncCallback) lastfmRestCB,NULL,NULL,NULL);
    g_object_unref(call);

    // Label the current track as "now playing" submitted, so that
    // we don't resubmit if we're called again.
    currTrack.submittedNowPlaying = true;
  }
}

/*
 * Submit a track playing using last.fm
 */
void lastfmSubmitTrack (struct trackTag currTrack)
{
  char startedPlaying[14];
  char trackLength[8];
  RestProxyCall *call;

  /*
   * Don't submit if
   */
  if(
      // We have already scrobbled
      currTrack.scrobbled ||
      // Last.fm is disabled
      !lastfmEnabled ||
      // Track is less than 30 seconds
      currTrack.trackLenSeconds < 30 ||
      // We have no track name or track artist
      !currTrack.hasBasicInfo
      )
  {
    return;
  }

  if(currTrack.trackName[0] != 0 && currTrack.trackArtist[0] != 0)
  {
    call = rest_proxy_new_call(lastfmConnectProxy);
    rest_proxy_call_set_function(call,"track.scrobble");
    rest_proxy_call_add_param(call,"track",currTrack.trackName);
    rest_proxy_call_add_param(call,"artist",currTrack.trackArtist);
    sprintf(startedPlaying,"%d",currTrack.startedPlaying);
    rest_proxy_call_add_param(call,"timestamp",startedPlaying);
    sprintf(trackLength,"%d",currTrack.trackLenSeconds);
    rest_proxy_call_add_param(call,"duration",trackLength);
    if(currTrack.hasAlbum)
    {
      rest_proxy_call_add_param(call,"album",currTrack.trackAlbum);
    }
    rest_proxy_call_set_method(call,"POST");
    rest_proxy_call_async(call, (RestProxyCallAsyncCallback) lastfmRestCB,NULL,NULL,NULL);
    g_object_unref(call);

    // Label the current track as scrobbled, so that we don't resubmit if
    // we're called again.
    currTrack.scrobbled = true;
  }
}


