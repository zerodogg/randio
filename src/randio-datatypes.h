#include <gtk/gtk.h>

struct trackTag
{
  char trackName[254];
  char trackArtist[254];
  char trackAlbum[254];
  char trackLength[254];
  char *currTrackPath;
  int trackLenSeconds;
  int trackID;
  bool submittedNowPlaying;
  bool scrobbled;
  bool hasBasicInfo;
  bool hasAlbum;
  bool notificationDisplayed;
  int startedPlaying;
  GMutex lock;
};

struct randioGlobalStateStruct
{
  GtkWidget *mainWindow;
  GtkApplication *app;
  GtkBuilder *uiBuilder;
};
