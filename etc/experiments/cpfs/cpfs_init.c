/**
 *
 * CPFS
 *
 * Copyright (C) 2016-2016 Dmitry Zavalishin, dz@dz.ru
 *
 * Init/shutdown.
 *
 *
**/

#include "cpfs.h"
#include "cpfs_local.h"


errno_t 		cpfs_init(void)
{
    errno_t rc;

    rc = cpfs_init_sb();
    if( rc ) return rc;

}


errno_t cpfs_stop(void)
{

    // TODO check locked rcords
    // TODO flush?
}

