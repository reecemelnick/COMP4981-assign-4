#include "../include/database.h"
#include "../include/display.h"
#include <ndbm.h>
#include <stdio.h>
#include <stdlib.h>

// Processes: Server --> Monitor --> Workers
// socketpair
// fork
// socket/bind/listen
// select/poll
// accept add to select/poll

int main(void)
{
    DBM *db;

    display("---Robust Server---");

    db = create_database();

    insert_user_to_db(db);

    return EXIT_SUCCESS;
}
