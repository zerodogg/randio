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
#define STUB printf("STUB: %s at %s:%d\n",__FUNCTION__,__FILE__,__LINE__)

void initUI (void);
bool playTrack (int trackID);
void addFileToLib (char *file);
void playFile (char *file);
void tagInfo (const GstTagList *list, const gchar *tag, gpointer user_data);
void displayTrackNotification (void);
static gboolean gstMessage (GstBus *bus, GstMessage *msg, gpointer user_data);
void togglePlaying (void);
void nextTrackInThread (void);
void nextTrack (void);
void banTrack (void);
void loveTrack (void);
void initGST (void);
void buildUI (void);
void buildGAction (const char *name, void *funcPtr);
void handleTagMessage (GstTagList *tags);
void updateLabel (void);
void updateWinStateInfo (void);
void showAboutBox (void);
void clearCurrent (void);
static void closeApp (void);
void handleMediaKeyEvent (GDBusProxy *proxy, gchar *sender_name, gchar *signal_name, GVariant *parameters, gpointer user_data);
void initMediaKeys (void);
int trackDuration (void);
void loadConfig (void);
gboolean tick(void);
char* getConfDir (void);
static void destroyApp (GtkWidget *widget, gpointer data);
void gtkMainIteration (void);
int trackPosition (void);
void setTrackDuration (void);
void app_init (GApplication *app, gpointer user_data);
void app_activate (GApplication *app, gpointer user_data);
int main (int argc, char *argv[]);

/* Shared global variables */
extern bool playOnlyLoved;
extern struct trackTag currTrack;
