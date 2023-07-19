
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>

#include "comp.h"

/*
char *get_dtype_name( unsigned char dtype )
{
    switch ( dtype ) {
    default:            break;
    case DT_UNKNOWN:    return "of undetermined type";
    case DT_DIR:        return "directory";
    case DT_REG:        return "regular file";
    }
    return "of other type";
}
*/

static void traverse_directory( char *path, map_t *map, unsigned long *countp )
{
//    printf( "Entering directory %s\n", path );
    DIR *ref_dir = opendir( path );
    if ( NULL == ref_dir ) {
        printf( "Unable to open directory %s (errno %d) - exiting\n", path, errno );
        exit(1);
    }

    while ( 1 ) {
        struct dirent *ref_de = readdir( ref_dir );
        if ( NULL == ref_de ) {
            break;
        }

        unsigned char ref_detype = ref_de->d_type;
        char * ref_dename = ref_de->d_name;
        if ( ref_dename[0] == '.' ) {
            if ( ref_dename[1] == 0 || ( ref_dename[1] == '.' && ref_dename[2] == 0 ) )
                continue;       // skip parent and current directory
        }

        int cur_path_size = strlen(path);
        char *new_path = malloc( cur_path_size + strlen(ref_dename) + 2 );
        strcpy( new_path, path );
        new_path[cur_path_size] = '/';
        strcpy( new_path + cur_path_size + 1, ref_dename );

        struct stat stat_data;
        int res;

        switch ( ref_detype ) {
        case DT_REG:
            res = stat( new_path, &stat_data );
            if ( res != 0 ) {
                printf( "unable to stat regular file %s\n", new_path );
                exit(1);
            }
//            printf( "size %ld, path %s\n", stat_data.st_size, new_path );
            off_t size = stat_data.st_size;
            if ( 0 != size ) {
                ++ *countp;
                map_insert_entry( map, (void *)size, (void*)new_path );
                // do not free new path here as it is the value in the hash
                // table, (while size is the key)
            } else {
                printf( "Empty file %s\n", new_path );
                free( new_path );
            }
            break;
        case DT_DIR:
            traverse_directory( new_path, map, countp );
            free( new_path );
            break;
        default:
            printf( "Skipping special file %s\n", new_path );
            free( new_path );
            break;
        }
    }
    closedir( ref_dir );
}

typedef struct {
    char **name_array;
    uint32_t n_names;
    uint32_t size;

    bool compare;
    bool remove;
    bool interactive;
    bool confirm;
} file_attr_t;

#define MAX_STATIC_BUFFER_SIZE  (2 * 1024 * 1024 )
static char buffer1[ MAX_STATIC_BUFFER_SIZE ];
static char buffer2[ MAX_STATIC_BUFFER_SIZE ];
static bool bin_compare( FILE *f1, FILE *f2 )
{
    fseek( f1, 0, SEEK_SET );       // f1 is used multiple times
    size_t n1 = fread( buffer1, 1, MAX_STATIC_BUFFER_SIZE, f1 );
    size_t n2 = fread( buffer2, 1, MAX_STATIC_BUFFER_SIZE, f2 );
    if ( n1 != n2 ) return false;   // should never happen

    for ( size_t i = 0; i < n1; ++ i ) {
        if ( buffer1[i] != buffer2[i] )
            return false;
    }
    return true;
}

typedef struct _name_list {
    struct _name_list   *next;
    struct _name_list   *prev;
    char                *name;
} name_list_t;

bool iremove = false;

static void interactive_remove_files( name_list_t *names, int nnames, bool confirm )
{
    while ( true ) {
        printf( "> Enter the space separated list of name indexes to remove: " );
        fflush( stdout );
        char *buffer = malloc( 20 * nnames );   // some extra space
        int n = read( fileno(stdin), buffer, 20 * nnames );
        if ( n > 1 ) {
            char **to_remove = malloc( sizeof(char *) * (nnames + 1) );
            char *end = buffer;
            int  k = 0;
            --n;

            while ( end - buffer < n ) {
                while ( *end == ' ' && end - buffer < n ) ++end;
                long int val = strtol( end, &end, 10 );
//                printf( "val %ld nnames %d\n", val, nnames );
                if ( val < nnames ) {
                    name_list_t *cur = names;
                    for ( int i = 0; i < val; ++i ) {
                        cur = cur->next;
                    }
                    to_remove[k++] = cur->name;
                }
                while ( *end == ' ' && end - buffer < n ) ++end;
            }
            if ( k > 0 ) {
                bool do_remove = false;
                if ( confirm ) {
                    printf( " Confirm removing files:\n" );
                    for ( int i = 0; i < k; ++i ) {
                        printf( "  %s\n", to_remove[i] );
                    }
                    printf( "> Enter Y or N: ");
                    fflush( stdout );
                    int n = read( fileno(stdin), buffer, 10 );
                    if ( n == 2 && (buffer[0] & 0x5f) == 'Y' ) {
                        do_remove = true;
                    }
                } else {
                    do_remove = true;
                }
                if ( do_remove ) {
                    for ( int i = 0; i < k; ++i ) {
                        if ( -1 == remove( to_remove[i] ) ) {
                            printf( "Failed to remove %s\n", to_remove[i] );
                        }
                    }
                    free(to_remove);
                    free( buffer );
                    return;
                }
            }
            free(to_remove);
        } else {
            printf( "Leaving both files\n" );
            free( buffer );
            return;
        }
    }
}

static int redundant = 0;
static void bin_compare_all( uint32_t size, name_list_t *list,
                             bool remove, bool interactive, bool confirm )
{
    if ( NULL == list || NULL == list->next ) return;   // 0 or 1 name

    name_list_t *same;

    while ( true ) {
        same = list;        // move head of list in same (tentatively)
        list = list->next;  // remove head from list
        list->prev = NULL;
        same->next = NULL;
        same->prev = NULL;
        name_list_t *last_same = same;

        FILE *f1 = fopen( same->name, "rb" );
        if ( NULL == f1 ) {
            printf( "Failed to open file %s (errno %d) exiting\n", same->name, errno );
            exit(1);
        }

        name_list_t *next_item;
        for ( name_list_t *item = list; item; item = next_item ) {
            FILE *f2 = fopen( item->name, "rb" );
            if ( NULL == f2 ) {
                printf( "Failed to open file %s (errno %d) exiting\n", item->name, errno );
                exit(1);

            }
            next_item = item->next;
            if ( bin_compare( f1, f2 ) ) {  // same: move item to same list
                if ( NULL != item->prev ) {
                    item->prev->next = item->next;
                } else {
                    list = item->next;
                }
                if ( NULL != item->next ) {
                    item->next->prev = item->prev;
                }
                last_same->next = item;
                item->next = NULL;
                item->prev = last_same;
                last_same = item;
            }
            fclose( f2 );
        }
        if ( same->next ) {
            name_list_t *cur = same;
            int nnames = 0;
            printf( "size %d\n", size );
            while ( cur ) {
                printf( "  %s\n", cur->name );
                ++nnames;
                cur = cur->next;
            }
            redundant += nnames - 1;    // all but one are redundant
            if ( remove ) {    // ask which names to remove (sep with ' ')
                if ( interactive ) {
                    interactive_remove_files( same, nnames, confirm );
                }
            }
        }
        fclose( f1 );
        if ( NULL == list || NULL == list->next )
            break;
    }
}

static void display( file_attr_t *fattr )
{
    if ( true == fattr->compare ) {
        name_list_t *list = malloc( sizeof(name_list_t) * fattr->n_names );
        name_list_t *prev = NULL;
        size_t i = 0;
        for ( ; i < fattr->n_names; ++ i ) {
            list[i].prev = prev;
            list[i].name = fattr->name_array[i];
            prev = &list[i];
            list[i].next = &list[i+1];
        }
        list[i-1].next = NULL;
        bin_compare_all( fattr->size, list, fattr->remove, fattr->interactive, fattr->confirm );
        free( list );
    } else {
        printf( "size %d\n", fattr->size );
        for ( size_t i = 0; i < fattr->n_names; ++ i ) {
            printf( " %s\n", fattr->name_array[i] );
        }
    }
}

#if 0
static void visit( size_t hash, int remaining,
                   void *key, void *data, void *private )
{
    (void)data;
    file_attr_t *fattr = private;

//    printf( "visiting hash=%ld, key=%ld, remaining=%d, path=%s\n",
//            hash, (size_t)key, remaining, (char *)data );
    ++fattr->hash_count;

    if ( fattr->last_hash != hash ) {
        if ( 0 == remaining ) {
            return;   // skip single
        }

        fattr->n_names = remaining + 1;
        fattr->name_array = malloc( fattr->n_names * sizeof( char *) );
        if ( NULL == fattr->name_array ) {
            printf("Ran out of space, exiting\n");
            exit(1);
        }
        fattr->name_array[remaining] = (char *)key;
//        printf( "size %ld\n %s", hash, (char *)key );
        fattr->last_hash = hash;
    } else {
//        printf( "\n %s", (char *)key );
        fattr->name_array[remaining] = (char *)key;
        if ( 0 == remaining ) {
//            printf("\n");
            display( fattr );
            free( fattr->name_array );
            fattr->name_array = NULL;
        }
    }
}
#endif

#define INITIAL_HASH_SIZE   2048
#define MAX_COLLISIONS      2047

typedef struct {
    uint64_t    size;
    char      * path;
} map_entry;

static void visit_entries( uint32_t n, map_entry *entries, file_attr_t *fattr )
{
    struct size_count { uint64_t s; int n; };   // n entries for size s
    struct size_count *sc = malloc( sizeof(struct size_count) * n );

    sc[0].s = entries[0].size;
    sc[0].n = 1;
    uint32_t count = 1;

    for ( uint32_t i = 1; i < n; ++i ) {
        uint32_t j = 0;
        for ( ; j < count; ++j ) {
            if ( entries[i].size == sc[j].s ) {
                ++sc[j].n;
                break;
            }
        }
        if ( j == count ) { // new size, start a new list
            sc[count].s = entries[i].size;
            sc[count].n = 1;
            ++ count;
        }
    }

    for ( uint32_t j = 0; j < count; ++j ) {
        int n_names = sc[j].n;
        if ( n_names != 1 ) {   // not an isolated file;

            fattr->name_array = malloc( n_names * sizeof( char *) );
            fattr->n_names = n_names;
            fattr->size = sc[j].s;
            uint32_t k = 0;

            for ( uint32_t i = 0; i < n; ++i ) {
                if ( entries[i].size == sc[j].s ) {
                    fattr->name_array[k++] = entries[i].path;
                }
            }
            display( fattr );
            free( fattr->name_array );
        }
    }
    free( sc );
}

static uint32_t collected_count = 0;
static map_entry collected_entries[ MAX_COLLISIONS + 1 ] = { 0 };

static bool map_collect( uint32_t index,
                         const void *key, const void *data, void *ctxt )
{
    static uint64_t current_index = 0xffffffffffffffff;

    file_attr_t *fattr = (file_attr_t *)ctxt;
    if ( current_index != index ) {
        if ( collected_count > 1 ) {
            visit_entries( collected_count, collected_entries, fattr );
        }
//        printf( "-- new index %d\n", index );
        current_index = index;
        collected_entries[0].size= (uint64_t)key;
        collected_entries[0].path = (char *)data;
        collected_count = 1;

    } else {
        collected_entries[collected_count].size = (uint64_t)key;
        collected_entries[collected_count].path = (char *)data;
        ++collected_count;
//        if ( collected_count > 2 ) {
//            printf( "###### map_collect: more than 2 entries for index %d\n",
//                    index );
//        }
    }
    return false;
}

extern void check_files( map_t *map, bool compare,
                         bool remove, bool interactive, bool confirm )
{
    file_attr_t fattr;
    fattr.name_array = NULL;
    fattr.n_names = 0;
    fattr.compare = compare;
    fattr.remove = remove;
    fattr.interactive = interactive;
    fattr.confirm = confirm;

    map_process_entries( map, map_collect, (void *)&fattr );
    visit_entries( collected_count, collected_entries, &fattr );

    if ( ! remove ) {
        printf( "Found %d redundant files\n", redundant );
    }
}

static uint64_t hash_data( const void *key )
{
    return (uint64_t)key;
}

static bool same_data( const void *key1, const void *key2 )
{
    (void)key1;
    (void)key2;
    return false;
}

extern map_t *collect_similar_files( char *root_path )
{
    // By default start with a medium size map table.
    // The collision threshold is very high on purpose. because was are
    // creating a collision for each file with the same size (in addition
    // to the actual collisions that a modulo can create).
    map_t *map = new_map( hash_data, same_data,
                          INITIAL_HASH_SIZE, MAX_COLLISIONS );
    if ( NULL == map ) {
        return NULL;
    }

    unsigned long count = 0;
    traverse_directory( root_path, map, &count );
    printf( "Traversed %ld files\n", count );

    return map;
}

static bool free_entry( uint32_t index,
                        const void *key, const void *data, void *ctxt )
{
    (void)index;
    (void)key;
    (void)ctxt;

    free( (void *)data );
    return false;
}

extern void free_collected_data( map_t *map )
{
    map_process_entries( map, free_entry, NULL );
    map_free( map );
}
