struct randioScanProgress
{
  GtkListStore *listStore;
  GtkTreeIter currEntryIter;
  char *dir;
  int currPulse;
};

bool scanDir (char *dir, int depth, struct randioScanProgress *progress);
void showPrefs (GSimpleAction *simple,GVariant *parameter, gpointer user_data);
void startScan (char *dir, GtkTreeIter iter, GtkListStore *store);
void addFileToLib (char *file);
void runThreadedScan (gpointer *user_data);
void removeDirectoryFromLib (GtkButton *button, struct randioGlobalStateStruct *randioGlobalState);
void initializePrefs(struct randioGlobalStateStruct *randioGlobalState, GtkWindow *prefsWin, GtkTreeView *treeView);
void selectDirectoryForLib (GtkButton *button, struct randioGlobalStateStruct *randioGlobalState);
