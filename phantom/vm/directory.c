/**
 *
 * Phantom OS
 *
 * Copyright (C) 2005-2016 Dmitry Zavalishin, dz@dz.ru
 *
 * Directory: hash map of strings to pvm objects
 *
 *
**/

#define DEBUG_MSG_PREFIX "vm.dir"
#include <debug_ext.h>
#define debug_level_flow 10
#define debug_level_error 10
#define debug_level_info 10

#include <hashfunc.h>
#include <vm/syscall.h>
#include <vm/object.h>
#include <vm/alloc.h>
#include <spinlock.h>

/*
#include <phantom_libc.h>
#include <time.h>
#include <threads.h>

#include <kernel/snap_sync.h>
#include <kernel/vm.h>
#include <kernel/atomic.h>

#include <vm/root.h>
#include <vm/exec.h>
#include <vm/bulk.h>

#include <vm/p2c.h>

#include <vm/wrappers.h>


#include <video/screen.h>
#include <video/vops.h>
#include <video/internal.h>
*/

#warning lock
#define LOCK_DIR(__dir)
#define UNLOCK_DIR(__dir)


/**
 *
 * BUG! TODO no mutex, TODO general short-term sync primitive for persistent mem
 *
 * General design:
 *
 * 3 arrays, array of strings (keys), array of obvects (values), bit array of flags - single or multiple value in dir entry.
 * Hash func points to array slot.
 * If we have just one entry for given key, just hold it and its key in corresponding arrays. Have 0 in multiple flag array.
 * If we have more than one entry for given key, hold 2nd level arrays (for values and keys) in corresponding arrays. Have 1 in multiple flag array.
 * In 2nd level arrays key and value positions are correspond too. Null in key slot menas that slot is unused.
 *
**/

typedef struct hashdir {
    u_int32_t                           capacity;       // size of 1nd level arrays
    u_int32_t                           nEntries;       // number of actual entries stored

    struct pvm_object   		keys;      	// Where we actually hold keys
    struct pvm_object   		values;      	// Where we actually hold values
    u_int8_t 				*flags;      	// Is this keys/values slot pointing to 2nd level array

    hal_spinlock_t                      lock;

} hashdir_t;

static int hdir_cmp_keys( const char *ikey, size_t ikey_len, pvm_object_t okey );

static errno_t hdir_find( hashdir_t *dir, const char *ikey, size_t i_key_len, pvm_object_t *out )
{
    if( dir->nEntries == 0 ) return ENOENT;

    assert(dir->capacity);
    assert(dir->keys.data != 0);
    assert(dir->values.data != 0);
    assert(dir->flags != 0);

    LOCK_DIR(dir);

    int keypos = calc_hash( ikey, ikey+i_key_len );

    pvm_object_t okey = pvm_get_array_ofield( dir->keys.data, keypos );
    if( pvm_is_null( okey ) )
    {
        UNLOCK_DIR(dir);
        return ENOENT;
    }

    u_int8_t flags = dir->flags[keypos];

    // No indirection?
    if( flags == 0 )
    {
        if( 0 == hdir_cmp_keys( ikey, i_key_len, okey ) )
        {
            *out = pvm_get_array_ofield( dir->values.data, keypos );
            UNLOCK_DIR(dir);
            return 0;
        }
    }

    // Indirect, find by linear search in 2nd level array
    // okey is arrray

    pvm_object_t valarray = pvm_get_array_ofield( dir->values.data, keypos );
    if( pvm_is_null( valarray ) )
    {
        lprintf("keyarray exists, valertray not"); // don't panic ;), but actually it is inconsistent
        UNLOCK_DIR(dir);
        return ENOENT;
    }

    size_t indir_size = get_array_size( dir->keys.data );

    int i;
    for( i = 0; i < indir_size; i++ )
    {
        pvm_object_t indir_key = pvm_get_array_ofield( dir->keys.data, i );

        if( 0 == hdir_cmp_keys( ikey, i_key_len, indir_key ) )
        {
            *out = pvm_get_array_ofield( valarray.data, i );
            UNLOCK_DIR(dir);
            return 0;
        }
    }

    UNLOCK_DIR(dir);
    return ENOENT;

}


//! Return EEXIST if dup
static errno_t hdir_add( hashdir_t *dir, const char *ikey, size_t i_key_len, pvm_object_t add )
{
    assert(dir->capacity);
    assert(dir->keys.data != 0);
    assert(dir->values.data != 0);
    assert(dir->flags != 0);

    LOCK_DIR(dir);

    int keypos = calc_hash( ikey, ikey+i_key_len );

    pvm_object_t okey = pvm_get_array_ofield( dir->keys.data, keypos );
    u_int8_t flags = dir->flags[keypos];

    // No indirection and key is equal
    if( (!flags) && !pvm_is_null( okey ) )
    {
        if( 0 == hdir_cmp_keys( ikey, i_key_len, okey ) )
        {
            UNLOCK_DIR(dir);
            return EEXIST;
        }
    }

    // No indirection and corresponding key slot is empty
    if( (!flags) && pvm_is_null( okey ) )
    {
        // Just put

        pvm_set_array_ofield( dir->keys.data, keypos, pvm_create_string_object_binary( ikey, i_key_len ) );
        pvm_set_array_ofield( dir->values.data, keypos, add );
        dir->nEntries++;

        UNLOCK_DIR(dir);
        return 0;
    }

    // No indirection, stored key exists and not equal to given
    // Need to convert 1-entry to indirection

    if( (!flags) && !pvm_is_null( okey ) && (0 != hdir_cmp_keys( ikey, i_key_len, okey )) )
    {
        pvm_object_t oval = pvm_get_array_ofield( dir->values.data, keypos );

        // Now create arrays
        pvm_object_t keya = pvm_create_array_object();
        pvm_object_t vala = pvm_create_array_object();

        // put old key/val
        pvm_append_array( keya.data, okey );
        pvm_append_array( vala.data, oval );

        // put new key/val
        pvm_append_array( keya.data, pvm_create_string_object_binary( ikey, i_key_len ) );
        pvm_append_array( vala.data, add );
        dir->nEntries++;

        pvm_set_array_ofield( dir->keys.data, keypos, keya );
        pvm_set_array_ofield( dir->values.data, keypos, vala );
        dir->flags[keypos] = 1;

        UNLOCK_DIR(dir);
        return 0;
    }

    assert(flags);
    assert(dir->capacity);
    assert(dir->keys.data != 0);
    assert(dir->values.data != 0);

    // Indirection. Scan and check for key to exist

    size_t indir_size = get_array_size( dir->keys.data );

    int i, empty_pos = -1;
    for( i = 0; i < indir_size; i++ )
    {
        pvm_object_t indir_key = pvm_get_array_ofield( dir->keys.data, i );
        if(pvm_is_null(indir_key))
        {
            empty_pos = i;
            continue;
        }

        // Have key
        if( 0 == hdir_cmp_keys( ikey, i_key_len, indir_key ) )
        {
            UNLOCK_DIR(dir);
            return EEXIST;
        }
    }

    // put new key/val
    if( empty_pos >= 0 )
    {
        pvm_set_array_ofield( dir->keys.data, empty_pos, pvm_create_string_object_binary( ikey, i_key_len ) );
        pvm_set_array_ofield( dir->values.data, empty_pos, add );
    }
    else
    {
        pvm_append_array( dir->keys.data, pvm_create_string_object_binary( ikey, i_key_len ) );
        pvm_append_array( dir->values.data, add );
    }
    dir->nEntries++;

    UNLOCK_DIR(dir);
    return 0;
}


static int hdir_cmp_keys( const char *ikey, size_t ikey_len, pvm_object_t okey )
{
    size_t okey_len = pvm_get_str_len(okey);

    if(okey_len > ikey_len) return 1;
    if(ikey_len > okey_len) return -1;

    const char *okeyp = pvm_get_str_data(okey);

    int i;
    for( i = 0; i < ikey_len; i++ )
    {
        if( *okeyp > *ikey ) return 1;
        if( *ikey > *okeyp ) return 1;
    }

    return 0;
}



















