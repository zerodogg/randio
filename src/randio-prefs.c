#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <gio/gio.h>

#include <sqlite3.h>

#include <rest-extras/lastfm-proxy.h>

#include "randio-datatypes.h"
#include "randio-sql.h"
#include "randio-lastfm.h"
#include "randio-prefs.h"

enum {
  DIR_PATH,
  DIR_SPINNER_ACTIVE,
  DIR_SPINNER_PULSE,
  DIR_DATA_ENTRIES
};

/* Show the prefs window */
void showPrefs (GSimpleAction *simple,
    GVariant      *parameter,
    gpointer       user_data)
{
  struct randioGlobalStateStruct *randioGlobalState;
  GtkWindow *prefsWin;
  GtkTreeView *treeView;

  randioGlobalState = user_data;

  g_assert( GTK_IS_BUILDER(randioGlobalState->uiBuilder) );

  prefsWin = GTK_WINDOW(gtk_builder_get_object(randioGlobalState->uiBuilder,"prefsWindow"));
  g_assert( GTK_IS_WINDOW(prefsWin));

  treeView = GTK_TREE_VIEW(gtk_builder_get_object(randioGlobalState->uiBuilder,"libraryView"));

  // If the treeView does not have a model, then we're being called for the first time.
  if(gtk_tree_view_get_model(treeView) == NULL)
  {
    initializePrefs(randioGlobalState,prefsWin,treeView);
  }

  gtk_widget_show_all(GTK_WIDGET(prefsWin));
}

void initializePrefs(struct randioGlobalStateStruct *randioGlobalState, GtkWindow *prefsWin, GtkTreeView *treeView)
{
  GtkListStore *store;
  GtkWidget *lastfmInfo;
  GtkWidget *lastfmConnectButton;
  GtkWidget *rmDirectory;
  GtkWidget *addDirectory;
  GtkTreeViewColumn *spinnerColumn;
  GtkCellRenderer *scanStateRenderer;
  unsigned char *user;
  sqlite3_stmt *statement;
  // First initialize the model
  store = gtk_list_store_new(DIR_DATA_ENTRIES,G_TYPE_STRING,G_TYPE_BOOLEAN,G_TYPE_INT);
  gtk_tree_view_set_model(treeView,GTK_TREE_MODEL(store));

  // Fetch the column and renderer for the spinner
  spinnerColumn = GTK_TREE_VIEW_COLUMN(gtk_builder_get_object(randioGlobalState->uiBuilder,"scanning-column"));
  scanStateRenderer = GTK_CELL_RENDERER(gtk_builder_get_object(randioGlobalState->uiBuilder,"scanStateRenderer"));
  // Binds the columns DIR_SPINNER_PULSE and DIR_SPINNER_ACTIVE on our store (model)
  // to the properties pulse and active on the state renderer column.
  // These are used to enable and render the spinner that indicates that we
  // are searching through the library.
  gtk_tree_view_column_add_attribute(spinnerColumn, scanStateRenderer, "pulse", DIR_SPINNER_PULSE);
  gtk_tree_view_column_add_attribute(spinnerColumn, scanStateRenderer, "active", DIR_SPINNER_ACTIVE);

  // Add our current directories to the list
  sqlite3_prepare_v2(db, "SELECT * FROM library", -1, &statement, NULL);
  while(sqlite3_step(statement) == SQLITE_ROW)
  {
    GtkTreeIter iter;
    const unsigned char *value = sqlite3_column_text(statement,0);
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter,DIR_PATH,value,-1);
  }
  sqlite3_finalize(statement);
  gtk_tree_view_columns_autosize(treeView);


  /*
   * Fetch the add/remove/lastfm buttons and connect the signals.
   * The signals could in theory be connected by GtkBuilder, but we need
   * to provide some data to the callbacks
   */
  rmDirectory = GTK_WIDGET(gtk_builder_get_object(randioGlobalState->uiBuilder,"removeButton"));
  addDirectory = GTK_WIDGET(gtk_builder_get_object(randioGlobalState->uiBuilder,"addButton"));
  lastfmConnectButton = GTK_WIDGET(gtk_builder_get_object(randioGlobalState->uiBuilder,"lastfmConnector"));
  g_signal_connect (rmDirectory, "clicked", G_CALLBACK(removeDirectoryFromLib), randioGlobalState);
  g_signal_connect (addDirectory, "clicked", G_CALLBACK(selectDirectoryForLib), randioGlobalState);
  g_signal_connect (lastfmConnectButton, "clicked", G_CALLBACK(lastfmConnect), prefsWin);

  // If we had existing directories, make the remove button active
  if(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(store),NULL) > 0)
  {
    gtk_widget_set_sensitive(rmDirectory,TRUE);
  }

  lastfmInfo = GTK_WIDGET(gtk_builder_get_object(randioGlobalState->uiBuilder,"lastfmConnectionStatus"));
  user = SQL_getSetting("lastfmUser");

  if(user == NULL)
  {
    gtk_label_set_text((GtkLabel*) lastfmInfo,"Randio can send your played tracks to last.fm");
    gtk_button_set_label((GtkButton*) lastfmConnectButton,"Connect to last.fm");
  }
  else
  {
    const char *format = "Randio is currently connected to last.fm as the user %s";
    char *label        = malloc(strlen(format)+strlen((const char*) user)+1);
    sprintf(label,format,user);
    gtk_label_set_text((GtkLabel*) lastfmInfo,label);
    gtk_button_set_label((GtkButton*) lastfmConnectButton,"Connect to another account");
    free(label);
    free(user);
  }
}

/*
 * **************************
 * Library scanning functions
 * **************************
 */

bool scanDir (char *dir, int depth, struct randioScanProgress *progress)
{
  DIR *currd  = opendir(dir);
  bool retVal = true;
  struct dirent *dirent;
  char *path;

  depth++;

  if(depth > 100)
  {
    printf("Directory tree too deep, giving up at %s\n",dir);
    return false;
  }

  while( (dirent = readdir(currd)) )
  {
    if(strcmp(dirent->d_name,".") == 0 || strcmp(dirent->d_name,"..") == 0 || strcmp(dirent->d_name,".git") == 0)
      continue;

    progress->currPulse++;
    gtk_list_store_set(progress->listStore, &progress->currEntryIter,DIR_SPINNER_PULSE,progress->currPulse/100,-1);

    path = malloc(strlen(dirent->d_name)+strlen(dir)+2);
    sprintf(path,"%s/%s",dir,dirent->d_name);

    /* If we can't read it, then just skip it */
    if (g_access(path,R_OK) != 0)
    {
      free(path);
      continue;
    }

    if(g_file_test(path, G_FILE_TEST_IS_DIR))
    {
      if (!scanDir(path,depth,progress))
      {
        free(path);
        retVal = false;
        break;
      }
    }
    else
    {
      // FIXME: Use gstdiscoverer instead
      if(g_regex_match_simple("\\.(mp3|ogg|flac)$",dirent->d_name,G_REGEX_CASELESS,0))
      {
        addFileToLib(path);
      }
    }

    free(path);
  }
  closedir(currd);
  return retVal;
}

void startScan (char *dir, GtkTreeIter iter, GtkListStore *store)
{
  // Has to be manually allocated since we need to pass the pointer to the worker thread
  struct randioScanProgress *progress = malloc(sizeof(struct randioScanProgress));
  // Needs to be duplicated since dir is on the stack
  progress->dir = strdup(dir);
  // Ditto for iter, it's on the stack in our parent
  memcpy(&progress->currEntryIter,&iter,sizeof(GtkTreeIter));
  progress->listStore = store;
  progress->currPulse = 0;
  g_thread_new("threadedScan", (GThreadFunc) runThreadedScan,progress);
}

void runThreadedScan (gpointer *user_data)
{
  struct randioScanProgress *progress = (struct randioScanProgress *) user_data;
  /* Use a transaction, otherwise SQLite attempts to flush the database
   * after each INSERT, which is incredibly slow */
  SQL_exec("BEGIN");
  scanDir(progress->dir,0,progress);
  SQL_exec("COMMIT");
  gtk_list_store_set(progress->listStore, &progress->currEntryIter,DIR_SPINNER_ACTIVE,FALSE,-1);
  gtk_list_store_set(progress->listStore, &progress->currEntryIter,DIR_SPINNER_PULSE,0,-1);
  // progress has been manually allocated by startScan
  free(progress);
}

/*
 * Adds a file to the library
 */
void addFileToLib (char *file)
{
  sqlite3_stmt *statement;
  sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM tracks WHERE path=?1", -1, &statement, NULL);
  sqlite3_bind_text(statement,1,file,-1,NULL);
  sqlite3_step(statement);
  int value = sqlite3_column_int(statement,0);
  sqlite3_finalize(statement);
  if(value == 0)
  {
    /* TODO: Find some way to speed up sqlite. We're going to insert a huge amount
     * of data, and don't need sqlite to flush on each insert */
    SQL_exec_1param("INSERT INTO tracks (path) VALUES (?1)",file);
  }
}

/*
 * Displays a directory selector, runs the scan (if needed), and inserts into
 * the database as required.
 */
void selectDirectoryForLib (GtkButton *button, struct randioGlobalStateStruct *randioGlobalState)
{
  char *filename;
  GtkTreeIter iter;
  GtkWindow *prefsWin;
  GtkListStore *store;
  GtkTreeView *treeView;

  g_assert( GTK_IS_BUILDER(randioGlobalState->uiBuilder));

  prefsWin = GTK_WINDOW(gtk_builder_get_object(randioGlobalState->uiBuilder,"prefsWindow"));
  treeView = GTK_TREE_VIEW(gtk_builder_get_object(randioGlobalState->uiBuilder,"libraryView"));

  g_assert( GTK_IS_WINDOW(prefsWin));

  store = GTK_LIST_STORE(gtk_tree_view_get_model(treeView));

  g_assert( GTK_IS_LIST_STORE(store) );

  GtkWidget *dialog = gtk_file_chooser_dialog_new(
      "Add directory",
      GTK_WINDOW( prefsWin ),
      GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
      "_Cancel",
      GTK_RESPONSE_CANCEL,
      "_Add",
      GTK_RESPONSE_ACCEPT,
      NULL
    );
  if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
  {
    filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
    gtk_widget_destroy (dialog);
    SQL_exec_1param("INSERT INTO library (path) VALUES (?1)",filename);
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter,0,filename,-1);
    gtk_list_store_set(store, &iter,DIR_SPINNER_ACTIVE,TRUE,-1);
    startScan(filename,iter, store);
    g_free(filename);
  }
  else
  {
    gtk_widget_destroy (dialog);
  }
}

/*
 * Removes a directory from the library.
 * Removes the entry from the list and the database */
void removeDirectoryFromLib (GtkButton *button, struct randioGlobalStateStruct *randioGlobalState)
{
  GtkTreeView *treeView;
  GtkWidget *rmDirectory;
  GtkTreeIter iter;
  GtkTreeModel *treeModel;
  GtkListStore *store;
  GValue value = G_VALUE_INIT;

  g_assert( GTK_IS_BUILDER(randioGlobalState->uiBuilder));

  treeView = GTK_TREE_VIEW(gtk_builder_get_object(randioGlobalState->uiBuilder,"libraryView"));
  store = GTK_LIST_STORE(gtk_tree_view_get_model(treeView));

  GList *selected = gtk_tree_selection_get_selected_rows( gtk_tree_view_get_selection(treeView), NULL);
  if(selected == NULL)
  {
    /* Nothing selected */
    return;
  }
  treeModel = gtk_tree_view_get_model(treeView);
  if (gtk_tree_model_get_iter(treeModel, &iter, selected->data))
  {
    gtk_tree_model_get_value( treeModel, &iter, 0, &value);
    SQL_exec_1param("DELETE FROM library WHERE path=?1",g_value_get_string(&value));
    g_value_unset(&value);
    gtk_list_store_remove(store, &iter);
  }
  /* Free entries in the list */
  g_list_foreach (selected, (GFunc) gtk_tree_path_free, NULL);
  /* Free the list */
  g_list_free (selected);
  /* Set the rm button inactive if needed */
  if(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(treeModel),NULL) > 0)
  {
    rmDirectory = GTK_WIDGET(gtk_builder_get_object(randioGlobalState->uiBuilder,"removeButton"));
    gtk_widget_set_sensitive(rmDirectory,TRUE);
  }
}
