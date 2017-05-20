#define LASTFM_APIKEY "38141fc93df034c5b12f0da5650dcdfd"
#define LASTFM_SECRET "c445850eb3aaf55744e8a8f14c046d13"

void lastfmRestCB (RestProxyCall *call, const GError *error);
char *getNodeContentFromREST (RestProxyCall *call, const char *node);
void lastfmConnect (GtkButton *button, GtkWindow *prefsWin);
void lastfmInit (void);
void lastfmSubmitCurrentlyPlaying (struct trackTag currTrack);
void lastfmSubmitTrack (struct trackTag currTrack);
