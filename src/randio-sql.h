extern sqlite3 *db;

void SQL_exec (const char *SQL);
void SQLite_init (char *confDir);
void SQL_exec_1param (const char *SQL, const char *param);
void initSQLite (void);
unsigned char* SQL_getSetting (const char *setting);
void SQL_setSetting (const char *key, const char *value);
int SQL_getTrackRND (bool playOnlyLoved, struct trackTag currTrack);
