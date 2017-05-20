/*
 * Randio music player
 * SQLite functions
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
#include <sqlite3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <glib.h>

#include "randio-datatypes.h"
#include "randio-sql.h"

/* The database pointer */
sqlite3 *db;

/*
 * Short form for a quick sqlite3_exec
 */
void SQL_exec (const char *SQL)
{
  char *error = NULL;
  sqlite3_exec(db, SQL, NULL, NULL, &error);
  if(error != NULL)
  {
    printf("Error from sqlite when executing statement '%s': %s\n",SQL,error);
    sqlite3_free(error);
  }
}

/*
 * Initialize SQLite, creating the database if needed. Called during startup
 */
void SQLite_init (char *confDir)
{
  const char *FNAM = "randio.sqlite";
  char *fpath;
  bool existed;

  fpath = malloc(strlen(confDir)+strlen(FNAM)+2);
  sprintf(fpath,"%s/%s",confDir,FNAM);
  existed = g_file_test(fpath,G_FILE_TEST_EXISTS);
  // FIXME: Check if the DB was properly opened
  if(sqlite3_config(SQLITE_CONFIG_SERIALIZED) != SQLITE_OK)
  {
    printf("Failed to set SQLite to SERIALIZED mode (required for threadsafety), expect trouble\n");
  }
  sqlite3_open(fpath, &db);

  if (!existed)
  {
    SQL_exec("CREATE TABLE tracks (track_id INTEGER PRIMARY KEY, path TEXT, banned TINYINT(1) DEFAULT 0);");
    SQL_exec("CREATE TABLE loved (track_id INT(11) PRIMARY KEY);");
    SQL_exec("CREATE TABLE settings (name VARCHAR(254) PRIMARY KEY, value VARCHAR(254));");
    SQL_exec("CREATE TABLE library (path VARCHAR(254) PRIMARY KEY);");
  }
  SQL_exec("CREATE TEMP TABLE played (track_id INTEGER PRIMARY KEY)");
  // TODO: Add a settings field containing version
  free(confDir);
  free(fpath);
}

/*
 * Retrieve a setting
 *
 * Note: this function returns NULL if the setting does not exist,
 * but it also returns NULL if the setting is set to NULL. A setting
 * should never be set to NULL, but it is something to be aware of.
 */
unsigned char* SQL_getSetting (const char *setting)
{
  sqlite3_stmt *statement;
  const unsigned char* sqliteValue;
  unsigned char* value = NULL;

  sqlite3_prepare_v2(db, "SELECT value FROM settings WHERE name=?1", -1, &statement, NULL);
  sqlite3_bind_text(statement,1,setting,-1,NULL);
  sqlite3_step(statement);
  sqliteValue = sqlite3_column_text(statement,0);
  if(sqliteValue != NULL)
  {
    value = (unsigned char*) strdup((char*) sqliteValue);
  }
  sqlite3_finalize(statement);
  return value;
}

/*
 * Set a setting
 */
void SQL_setSetting (const char *key, const char *value)
{
  const char *update = "UPDATE settings SET value=?1 WHERE name=?2";
  const char *insert = "INSERT INTO SETTINGS (value,name) VALUES (?1,?2)";
  sqlite3_stmt *statement;

  const char *expression;
  unsigned char *prevValue = SQL_getSetting(key);
  if(prevValue != NULL)
  {
    expression = update;
    free(prevValue);
  }
  else
  {
    expression = insert;
  }

  sqlite3_prepare_v2(db,expression,-1,&statement, NULL);
  sqlite3_bind_text(statement,1,value,-1,NULL);
  sqlite3_bind_text(statement,2,key,-1,NULL);
  sqlite3_step(statement);
  sqlite3_finalize(statement);
}

/*
 * Get a random track to play. Returns the track_id
 */
int SQL_getTrackRND (bool playOnlyLoved, struct trackTag currTrack)
{
  const char *from;
  char SQL[254];
  char WHERE[254] = "";
  sqlite3_stmt *statement;
  int trackID;

  if(playOnlyLoved)
  {
    from = "loved";
  }
  else
  {
    from = "tracks";
  }
  /*
   * If we have a currTrack, make sure we don't change to the same one
   */
  if(currTrack.trackID != -1)
  {
    sprintf(WHERE," AND track_id != %d ",currTrack.trackID);
  }
  sprintf(SQL,"SELECT track_id FROM %s WHERE banned != 1%s AND track_id NOT IN( SELECT track_id FROM played ) ORDER BY RANDOM() LIMIT 1",from,WHERE);

  sqlite3_prepare_v2(db, SQL, -1, &statement, NULL);
  int step = sqlite3_step(statement);

  trackID = sqlite3_column_int(statement,0);

  sqlite3_finalize(statement);

  if(step != SQLITE_ROW)
  {
    return -1;
  }

  return trackID;
}

/*
 * Runs an SQL statement that has a single bind parameter.
 * This is just a convenience function for simple inserts.
 */
void SQL_exec_1param (const char *SQL, const char *param)
{
    sqlite3_stmt *statement;
    sqlite3_prepare_v2(db,SQL,-1,&statement, NULL);
    sqlite3_bind_text(statement,1,param,-1,NULL);
    sqlite3_step(statement);
    sqlite3_finalize(statement);
}
