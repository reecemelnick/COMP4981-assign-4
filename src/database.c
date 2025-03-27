#include "../include/database.h"
// #include <fcntl.h>
// #include <ndbm.h>
#include <stdio.h>

// #include <string.h>

// DBM *create_database(void)
// {
//     DBM *db;

//     db = dbm_open("mydatabase", O_RDWR | O_CREAT, 0644);    // NOLINT
//     if(!db)
//     {
//         perror("dbm_open");
//     }

//     printf("Database made...\n");

//     return db;
// }

void db(void)
{
    printf("db\n");
}

// int insert_user_to_db(DBM *db)
// {
//     datum key;
//     datum value;

//     char key_buff[]   = "username";
//     char value_buff[] = "roze_user";

//     key.dptr    = key_buff;
//     key.dsize   = strlen((const char *)key.dptr) + 1;
//     value.dptr  = value_buff;
//     value.dsize = strlen((const char *)value.dptr) + 1;

//     printf("Storing key: %s, size: %d\n", (char *)key.dptr, (int)key.dsize);
//     printf("Storing value: %s, size: %d\n", (char *)value.dptr, (int)value.dsize);

//     if(dbm_store(db, key, value, DBM_REPLACE) != 0)
//     {
//         perror("dbm_store");
//         return -1;
//     }

//     printf("Stored user...\n");

//     return 0;
// }
