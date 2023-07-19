
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <malloc.h>

#include "hash.h"

// In this implementation, keys and data are defined as void pointers, but the
// actual data pointed to is never accessed by the code here. This allows a
// caller to pass anything that can be turned into a void *, such as a small
// or large integer (uint8_t to uint32_t or even uint64_t - in case of 64-bit
// architectures) as data or key, as long as (void *)0 is not inside the valid
// key range, without allocating any memory.

typedef struct _hash_entry {
    struct _hash_entry *next; // linked list in case of collisions
    void               *key;  // NULL for an unused entry in table
    void               *data;
    size_t             hash;
} hash_entry_t;

typedef struct _hash_table {
    unsigned int      nb;
    unsigned int      allocated;
    unsigned int      modulo;
    unsigned int      max_collision;
    hash_fct          hash_data;
    hash_entry_t      *table;

} hash_table_t;

static inline size_t count_entry_collisions( hash_entry_t *entry )
{
    size_t count = 0;
    while ( entry->next ) {
        entry = entry->next;
        ++count;
    }
    return count;
}

static size_t add_entry( hash_entry_t *entry, size_t hash, void *key, void *data )
{
    size_t count = 0;

    if ( entry->key ) {
        ++count;
        while ( entry->next ) {
            entry = entry->next;
            ++count;
        }

        hash_entry_t *new_entry = malloc( sizeof(hash_entry_t) );
        entry->next = new_entry;
        entry = new_entry;
    }

    entry->next = NULL;
    entry->key  = key;
    entry->data = data;
    entry->hash = hash;

    return count;
}

static void rehash( hash_table_t *ht, size_t old_size, hash_entry_t *new_table )
{
  /* for each non-empty entry, apply the new modulo
     and copy the old entry into the new location */

    ht->nb = 0;
    for ( unsigned int i = 0; i < old_size; i ++ ) {
        hash_entry_t *entry = &ht->table[ i ];

        if ( entry->key ) { /* a valid  entry */
            bool first = true;

            while ( entry ) {
                unsigned int index = entry->hash % ht->modulo;
                assert( index < ht->allocated );
                unsigned int collisions = add_entry( &new_table[ index],
                                            entry->hash, entry->key, entry->data );

                if ( ht->max_collision < collisions )
                    ht->max_collision = collisions;

                // free old collision list
                hash_entry_t *next = entry->next;
                if ( ! first ) {
                    free( entry );
                } else {
                    first = false;
                }

                ++ht->nb;
                entry = next;
            }
        }
    }
    free( ht->table );
    ht->table = new_table;
}

#define MIN_ALLOCATED   8   // must be power of 2
#define MAX_COLLISIONS  1024

static unsigned int get_prime( unsigned int size )
{
  int i;
  static const unsigned int greatest_prime[] = {
    1,          /* 1 */
    2,          /* 2 */
    3,          /* 4 */
    7,          /* 8 */
    13,         /* 16 */
    31,         /* 32 */
    61,         /* 64 */
    127,        /* 128 */
    251,        /* 256 */
    509,        /* 512 */
    1021,       /* 1024 */
    2039,       /* 2048 */
    4093,       /* 4096 */
    8191,       /* 8192 */
    16381,      /* 16384 */
    32749,      /* 32768 */
    65521,      /* 65536 */
    131071,     /* 131072 */
    262139,     /* 262144 */
    524287,     /* 524288 */
    1048573,    /* 1048576 */
    2097143,    /* 2097152 */
    4194301,    /* 4194304 */
    8388593,    /* 8388608 */
    16777213,   /* 16777216 */
    33554393,   /* 33554432 */
    67108859,   /* 67108864 */
    134217689,  /* 134217728 */
    268435399,  /* 268435456 */
    536870909,  /* 536870912 */
    1073741789, /* 1073741824 */
    2147483647, /* 2147483648 */
    4294967291  /* 4294967296 */
  };

  /* assuming size is a power of 2 */
  for ( i = 0; size ; i++ )
    size >>= 1;
  return greatest_prime[i-1];
}

hash_table_t * new_hash_table( size_t n_entries, hash_fct hash_data )
{
    hash_table_t *ht = malloc( sizeof( hash_table_t ) );

    ht->nb = 0;
    ht->max_collision = 0;
    ht->hash_data = hash_data;
    if ( n_entries ) {
        // roundup to the next power of 2
        int i = 0;
        for ( ; n_entries; ++i ) {
            n_entries >>= 1;
        }
        n_entries = 1 << i;
        ht->allocated = n_entries;
        ht->table = malloc( sizeof( hash_entry_t ) * n_entries );
        memset( (void *)ht->table, 0, sizeof(hash_entry_t) * n_entries );
    } else {
        ht->allocated = 0;
        ht->table = NULL;
    }
    ht->modulo = get_prime( ht->allocated );
    printf( "New hash table allocated with %d entries\n", ht->allocated );
    return ht;
}

// returns +1 if the table has been sucessfully re-allocated, -1 if it failed
// the re-allocation and 0 if it did not have to re-allocate the table.
static int make_room( hash_table_t *ht )
{
    // if less than 25% left or more than MAX_COLLISIONS colliding entries
    // in list, double the size
    if ( ( 4 * (1 + ht->nb) >= 3 * (ht->allocated) ) /* || 
           ht->max_collision > MAX_COLLISIONS*/ ) {
        printf("Making room\n");
        size_t old_size = ht->allocated;

        // starting from MIN_ALLOCATED, double the size
        size_t new_allocated = ( old_size ) ?  2 * old_size : MIN_ALLOCATED;

        hash_entry_t *new_table = malloc( sizeof(hash_entry_t) * new_allocated );
        if ( NULL == new_table )
            return -1;

        memset( (void *)new_table, 0, sizeof(hash_entry_t) * new_allocated );

        ht->allocated = new_allocated;
        ht->modulo = get_prime( new_allocated );
        ht->max_collision = 0;

        if ( ht->table ) {
            rehash ( ht, old_size, new_table );
        } else {
            ht->table = new_table;
        }
        return 1;
    }
    return 0;
}

void hash_table_traverse( hash_table_t *ht, visit_fct visit, void *private )
{
    static int traversal_count = 0;
    for ( size_t i = 0; i < ht->allocated; ++i ) {

        void *key = ht->table[i].key;
        if ( NULL != key ) {                // valid entry
            ++traversal_count;

            // the same list of collision entries can hold several hash values
            // due to the modulo applied to is in order to get the entry index
            // in the hash table. Potentially, a number, from 1 to up to the
            // maximum collision value, of different hash values can be found
            // in that list. In a first phase, collect all different hash values
            // and store them in an array of tuples (hash, count), then use that
            // array to generate the list of entries with the same hash.

            struct _hash_count { size_t h; int c; };
            struct _hash_count hc[ MAX_COLLISIONS + 1 ];

            int count = 1;                  // at least one hash_count tuple
            hc[0].h = ht->table[i].hash;    // first entry for the first hash
            hc[0].c = 1;                    // at least 1 entry for that hash

            for ( hash_entry_t *e = ht->table[i].next; e ; e = e->next ) {
                int j = 0;
                for ( ; j < count; ++j ) {
                    if ( e->hash == hc[j].h ) {
                        ++hc[j].c;
                        break;
                    }
                }
                if ( j == count ) {
                    hc[count].h = e->hash;
                    hc[count].c = 1;
                    ++count;
                }
            }

            for ( int j = 0; j < count; ++j ) {
                int remaining = hc[j].c - 1;
                for ( hash_entry_t *e = &ht->table[i]; e ; e = e->next ) {
                    if ( e->hash == hc[j].h ) {
                        visit( e->hash, remaining, e->key, e->data, private );
                        --remaining;
                    }
                }
            }
        }
    }
    printf( "Traversal count: %d\n", traversal_count );
}

static hash_entry_t *get_entry( hash_table_t *ht, void *key,
                                size_t *hashp )
{
    if ( NULL == ht ) {
        return NULL;
    }

    size_t hash = ht->hash_data( key );
    if ( hashp )
        *hashp = hash;

    if ( NULL == ht->table )
        return NULL;

    unsigned int index = hash % ht->modulo;
    hash_entry_t *entry = &ht->table[index];

    if ( NULL == entry->key ) {     // not in use
        return NULL;
    }

    while( entry ) {
        if ( entry->key == key )
            break;
        entry = entry->next;
    }
    return entry;
}

void *hash_table_lookup( hash_table_t *ht, void *key )
{
    hash_entry_t *entry = get_entry( ht, key, NULL );
    if ( NULL == entry )
        return NULL;
    return entry->data;
}

extern bool hash_table_add( hash_table_t *ht, void *key, void *data )
{
    size_t hash;
    hash_entry_t *entry = get_entry( ht, key, &hash );

    if ( hash == 0 ) {
        printf("Adding 0\n");
    }

    if ( NULL != entry ) {
        printf( "Same key entry (%s) in hash table\n", (char *)key );
        return false;                   // key is unique: reject same key
    }

    if ( -1 == make_room( ht ) ) {      // extend if needed
        printf( "make_room failed\n");
        return false;                   // sorry, no memory
    }
    size_t index = hash % ht->modulo;   // get possibly new index
    entry = &ht->table[index];
 
    size_t collisions = add_entry( entry, hash, key, data );
    if ( ht->max_collision < collisions )
        ht->max_collision = collisions;
    ++ht->nb;

    return true;
}

// does not attempt to shrink the hash table
extern bool hash_table_remove( hash_table_t *ht, void *key )
{
    size_t hash;
    hash_entry_t *entry = get_entry( ht, key, &hash );

    if ( NULL == entry ) return false;

    unsigned int index = hash % ht->modulo;
    hash_entry_t *prev = NULL, *cur = &ht->table[index];
    while ( cur != entry ) {
        prev = cur;
        cur = cur->next;
        assert( cur );
    }
    if ( prev ) {
        prev->next = entry->next;
        free( entry );
    } else {
        entry->key = entry->data = NULL;
    }
    --ht->nb;
    return true;
}

void debug_hash_table( hash_table_t *ht )
{
    printf( "Hash Table %p information\n", (void *)ht );
    printf( "  allocated room  %d\n", ht->allocated );
    printf( "  modulo          %d\n", ht->modulo );
    printf( "  max collisions  %d\n", ht->max_collision );
    printf( "  nb entries      %d\n\n", ht->nb );

    printf( "  Entries {\n" );
    hash_entry_t *entries = ht->table;
    if ( entries ) {
        for( unsigned int i = 0; i < ht->allocated; i++, entries++ ) {
            if ( entries->key ) {
#if 0
                printf( "   Index %d, key %zu, hash %zu\n",
                        i, (size_t)(entries->key), entries->hash );
#else
                printf( "   Index %d, key %s, hash %zu\n",
                        i, (char *)entries->key, entries->hash );
#endif
                for ( hash_entry_t *collide = entries->next; collide;
                                              collide = collide->next ) {
#if 0
                    printf( "   Index %d, key %zu, hash %zu\n",
                            i, (size_t)(collide->key), collide->hash );
#else
                    printf( "   Index %d, key %s, hash %zu\n",
                            i, (char *)collide->key, collide->hash );
#endif
                }
            }
        }
    }
    printf( "  }\n");
}

