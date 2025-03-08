
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <assert.h>

#define TIME_MEASURE 0

#if TIME_MEASURE
#include <time.h>   // only for timing measurements
#endif

#include "comp.h"

/*
    Use a simple map with file size as key. Since multiple files can have the
    same size, the data is a slice of all file paths with the same size.

    key = size -> data = slice of paths allocated in memory
*/
#if TIME_MEASURE
#define SEC_TO_NANOSEC(s)       ((s)*1000000000)
#define NANOSEC_TO_MILLISEC(n)  ((n)/1000000)
static int64_t get_nanosecond_timestamp( void )
{
    uint64_t nanoseconds;
    struct timespec ts;
    if ( -1 == clock_gettime(CLOCK_MONOTONIC_RAW, &ts) ) {
        printf("Failed to obtain timestamp. errno = %i: %s\n", errno,
               strerror(errno));
        nanoseconds = UINT64_MAX; // use this to indicate error
    } else {
        nanoseconds = SEC_TO_NANOSEC((uint64_t)ts.tv_sec) + (uint64_t)ts.tv_nsec;
    }
    return nanoseconds;
}

uint64_t file_stat_spent = 0;
#endif

// called for each path when traversing directories.
// return -1 in case of error, aborting the traversal,
//         0 if path is not used for other purpose, allowing it to be freed
//        +1 if path must NOT be freed by traverse_directory
typedef int (*process_file_t)( char *path, size_t size, void *context );

// traverse directories, from the root path, according to the argument nosub.
// if nosub is true, no subdirectory is entered, For each path in a directory,
// the function process is called with the given context.
static void traverse_directory( char *path, bool nosub,
                                process_file_t process, void *context)
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
        if ( '/' != path[cur_path_size-1]) {
            new_path[cur_path_size] = '/';
            ++cur_path_size;
        }
        strcpy( new_path + cur_path_size, ref_dename );

        struct stat stat_data;
        int res;

#if TIME_MEASURE
        uint64_t before_stat;
#endif
        switch ( ref_detype ) {
        case DT_REG:
#if TIME_MEASURE
        {
            before_stat = get_nanosecond_timestamp();
#endif
            res = stat( new_path, &stat_data );
#if TIME_MEASURE
            file_stat_spent += get_nanosecond_timestamp() - before_stat;
        }
#endif
            if ( res != 0 ) {
                printf( "unable to stat regular file %s\n", new_path );
                exit(1);
            }
//            printf( "size %ld, path %s\n", stat_data.st_size, new_path );
            switch ( process( new_path, stat_data.st_size, context ) ) {
            case -1:    // error, abort
                free( new_path );
                return;
            case 0:
                free( new_path );
                break;
            default:
                break;
            }
            break;
        case DT_DIR:
            if ( ! nosub ) {
                traverse_directory( new_path, nosub, process, context );
            }
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

#define MAX_STATIC_BUFFER_SIZE  (2 * 1024 * 1024 )
static bool bin_compare( FILE *f1, FILE *f2 )
{
    static char buffer1[ MAX_STATIC_BUFFER_SIZE ];
    static char buffer2[ MAX_STATIC_BUFFER_SIZE ];

    fseek( f1, 0, SEEK_SET );       // f1 may be used multiple times
    size_t n1 = fread( buffer1, 1, MAX_STATIC_BUFFER_SIZE, f1 );
    size_t n2 = fread( buffer2, 1, MAX_STATIC_BUFFER_SIZE, f2 );
    if ( n1 != n2 ) return false;   // should never happen, sizes are the same

    for ( size_t i = 0; i < n1; ++ i ) {
        if ( buffer1[i] != buffer2[i] )
            return false;
    }
    return true;
}

static unsigned long interactive_delete_duplicates( const char **path_array,
                                                    size_t  n_paths, bool confirm )
{
    bool    *del_index_array = malloc( sizeof(bool) * n_paths );
    memset( del_index_array, 0, sizeof(bool) * n_paths );
    char *buffer = malloc( 20 * n_paths ); // index size with some extra space

    printf( "> Enter the space separated list of name indexes to remove: " );
    fflush( stdout );
    bool do_remove = false;
    int n = read( fileno(stdin), buffer, 20 * n_paths );

    unsigned long removed = 0;
    if ( n > 1 ) {
        --n;                    // skip final char (\n)
        char *start = buffer;
        while ( start - buffer < n ) {
            while ( *start == ' ' && start - buffer < n ) ++start;
            char *end;
            long int index = strtol( start, &end, 10 );
            if ( end == start ) {
                break;  // consider as do not remove
            }
            //  printf( "index %ld n_paths %d\n", index, tct->n_paths );
            if ( index >= 0 || (size_t)index < n_paths ) {
                del_index_array[index] = true;
                do_remove = true;
            }
            start = end;
            while ( *start == ' ' && start - buffer < n ) ++start;
        }
        if ( do_remove && confirm ) {
            printf( " Confirm removing files:\n" );
            for ( size_t i = 0; i < n_paths; ++i ) {
                if ( del_index_array[i] ) {
                    printf( "  %s\n", path_array[i] );
                }
            }
            printf( "> Enter Y to remove: ");
            fflush( stdout );
            do_remove = false;
            int n = read( fileno(stdin), buffer, 10 );
            if ( n == 2 && (buffer[0] & 0x5f) == 'Y' ) {
                do_remove = true;
            }
        }
    }
    if ( do_remove ) {
        for ( size_t i = 0; i < n_paths; ++i ) {
            if ( del_index_array[i] ) {
                if ( -1 == remove( path_array[i] ) ) {
                    printf( "Failed to remove %s\n", path_array[i] );
                } else {
                    ++removed;
                }
            }
        }
    } else if ( confirm ) {
        printf( "Leaving all files\n" );
    }

    free( buffer );
    free( del_index_array );
    return removed;
}

typedef struct {
    unsigned long   instances;
    unsigned long   total;
    unsigned long   removed;
    bool            compare;
    bool            remove;
    bool            confirm;
} process_args_t;

#if TIME_MEASURE
uint64_t file_compare_spent = 0;
#endif

static void compare_redundant( slice_t *paths, size_t size, process_args_t *pargs )
{
    if ( NULL == pargs || NULL == paths ) return;

    size_t n_items = slice_len( paths );
    const char **same_items = malloc( n_items * sizeof( char *) );

    if ( NULL == same_items ) {
        printf( "Cannot allocate same item array, aborting\n" );
        exit(2);
    }

    for ( size_t i = 0; i < n_items; ++i ) {
        size_t n_same_items = 0;
        const char *path_1 = pointer_slice_item_at( paths, i );
        if ( NULL != path_1 ) {
            same_items[n_same_items++] = path_1;
            if ( pargs->compare ) {
                FILE *f1 = fopen( path_1, "rb" );
                if ( NULL == f1 ) {
                    printf( "Failed to open file %s (errno %d) exiting\n",
                            path_1, errno );
                    exit(1);
                }
                for ( size_t j = i+1; j < n_items; ++j ) {
                    const char *path_2 = pointer_slice_item_at( paths, j );
                    if ( NULL != path_2 ) {
                        FILE *f2 = fopen( path_2, "rb" );
                        if ( NULL == f2 ) {
                            printf( "Failed to open file %s (errno %d) exiting\n",
                                    path_2, errno );
                            exit(1);
                        }
#if TIME_MEASURE
                        uint64_t before_compare = get_nanosecond_timestamp();
#endif
                        if ( bin_compare( f1, f2 ) ) {              // same: copy item to
                            same_items[n_same_items++] = path_2;    // same_items and
                            pointer_slice_write_item_at( paths, j, NULL );  // remove from paths
                        }
#if TIME_MEASURE
                        file_compare_spent += get_nanosecond_timestamp() - before_compare;
#endif
                        fclose( f2 );
                    }
                }
                fclose( f1 );

            } else  {
                for ( size_t j = i+1; j < n_items; ++j ) {
                    const char *path_2 = slice_item_at( paths, j );
                    if ( NULL != path_2 ) {
                        same_items[n_same_items++] = path_2;
                        pointer_slice_write_item_at( paths, j, NULL );
                    }
                }
            }

            if ( n_same_items > 1 ) {
                ++pargs->instances;
                pargs->total += n_same_items;
                printf( "size %ld\n", size );
                for ( size_t j = 0; j < n_same_items; ++ j ) {
                    printf( "  %s\n", same_items[j] );
                }
                if ( pargs->remove ) {              // ask which names to remove
                    pargs->removed +=
                        interactive_delete_duplicates( same_items, n_same_items, pargs->confirm );
                }
            }
        }
    }
}

static bool compare_collected_entries( uint32_t index, const void *key,
                                       const void *data, void *ctxt )
{
    (void)index;
    process_args_t *pargs = (process_args_t *)ctxt;
    slice_t *paths = (slice_t *)data;
    if ( slice_len( paths ) > 1 ) { // duplicates exist
        if ( pargs->compare ) {     // compare, display and possibly remove
            compare_redundant( paths, (size_t)key, pargs );
        } else {                    // just display
            printf( "size %ld\n", (size_t)key );
            for ( size_t i = 0; i < slice_len( paths ); ++ i ) {
                printf( " %s\n", (const char *)pointer_slice_item_at( paths, i) );
            }
        }
    }
    return false;
}

typedef struct {
    char            *name;
    FILE            *target;
    size_t          size;

    unsigned long   instances;
    unsigned long   total;
    unsigned long   removed;

    map_t           *map;

    bool            absent;
    bool            compare;
    bool            remove;
    bool            confirm;

    bool            zero;
} target_context_t;

static void is_duplicate( const slice_t *paths, target_context_t *tcp )
{
    size_t n_items = slice_len( paths );
    const char **path_array = malloc( (1+n_items) * sizeof( char *) );
    if ( NULL == path_array ) {
        printf( "Cannot allocate path array, aborting\n" );
        exit(2);
    }
    path_array[0] = tcp->name;
    size_t n_paths = 1;

    for ( size_t i = 0; i < slice_len( paths ); ++i ) {
        const char *path = pointer_slice_item_at( paths, i );
        bool dup = ! tcp->compare;
        if ( ! dup ) {
            FILE *f = fopen( path, "rb" );
#if TIME_MEASURE
            uint64_t before_compare = get_nanosecond_timestamp();
#endif
            dup = bin_compare( tcp->target, f );
#if TIME_MEASURE
            file_compare_spent += get_nanosecond_timestamp() - before_compare;
#endif
            fclose( f );
        }
        if ( dup ) {
            path_array[n_paths++] = path;
        }
    }
    if ( n_paths > 1 ) {    // duplicates exist
                            // show and possibly remove them
        ++tcp->instances;
        tcp->total += n_paths;
        printf( "size %ld\n", tcp->size );
        for ( size_t i = 0; i < n_paths; ++ i ) {
            printf( " %s\n", path_array[i] );
        }
        if ( tcp->remove ) {
            tcp->removed += interactive_delete_duplicates( path_array, n_paths, tcp->confirm );
        }
    }
    free( path_array );
}

static void is_absent( const slice_t *paths, target_context_t *tcp )
{
    bool match = false;
    for ( size_t i = 0; i < slice_len( paths ); ++i ) {
        const char *path = pointer_slice_item_at( paths, i );

        FILE *f = fopen( path, "rb" );
#if TIME_MEASURE
        uint64_t before_compare = get_nanosecond_timestamp();
#endif
        if ( ! tcp->compare || bin_compare( tcp->target, f ) ) {
            match = true;   // at least one matching file found
        }
#if TIME_MEASURE
        file_compare_spent += get_nanosecond_timestamp() - before_compare;
#endif
        fclose( f );
    }
    if ( ! match ) {
        ++tcp->instances;
        printf( "%s content is not found in any path\n", tcp->name );
    }
}

static int check_target_content( char *path, size_t size, void *context )
{
    target_context_t *tcp = context;
    if ( 0 == size ) {
        if ( tcp->zero ) {
            printf( "Empty target file %s\n", path );
        }
        return 0;
    }
    tcp->name = path;
    tcp->size = size;
    tcp->target = fopen( path, "rb" );

    if ( NULL == tcp->target ) {
        printf( "Failed to open target file %s (errno %d) exiting\n",
                path, errno );
        exit(1);
    }
    const slice_t * paths = map_lookup_entry( tcp->map, (void *)size );
    if ( NULL != paths ) {
        if ( tcp->absent ) {
            is_absent( paths, tcp );
        } else {
            is_duplicate( paths, tcp );
        }
    } else if ( tcp->absent ) {
        ++tcp->instances;
        printf( "%s content is not found in any path\n", tcp->name );
    }
    fclose( tcp->target );
    return 0;
}

extern void process_same_size_files( map_t *map, args_t *args )
{
#if TIME_MEASURE
    file_stat_spent = 0;
    uint64_t file_process_start = get_nanosecond_timestamp();
#endif
    if ( NULL != args->target ) {
        target_context_t ctxt;
        ctxt.map = map;

        ctxt.instances = 0;
        ctxt.total = 0;
        ctxt.removed= 0;

        ctxt.zero = args->target->zero;
        ctxt.absent = args->absent;

        ctxt.compare = args->compare;
        ctxt.remove = args->remove;
        ctxt.confirm = args->confirm;

        struct stat stat_data;
        stat(args->target->path, &stat_data);
        if ( S_ISREG( stat_data.st_mode ) ) {       // Handle regular file
//            printf( "Target %s is a regular file\n", args->target->path );
            if ( 0 == stat_data.st_size ) {
                if ( args->target->zero ) {
                    printf( "Empty target file %s\n", args->target->path );
                }
                return;
            }
            check_target_content( args->target->path, stat_data.st_size, &ctxt );

        } else if ( S_ISDIR( stat_data.st_mode ) ){ // Handle directory
//            printf( "Target %s is a directory\n", args->target->path );
            ctxt.map = map;
            traverse_directory( args->target->path, args->target->nosub,
                                check_target_content, &ctxt );
        } else {
            printf( "Target %s is a special file: mode 0x%x - exiting\n",
                    args->target->path, stat_data.st_mode );
        }
        if ( ctxt.absent ) {
            printf( "Found %ld target files that do not exist in any path\n",
                   ctxt.instances );
        } else {
            printf( "Found %ld instances of redundant files, for a total of %ld redundant files\n",
                    ctxt.instances, ctxt.total );
            if ( ctxt.remove ) {
                printf( "Removed %ld redundant files\n", ctxt.removed );
            }
        }
    } else {
        process_args_t pargs;
        pargs.instances = 0;
        pargs.total = 0;
        pargs.removed = 0;

        pargs.compare = args->compare;
        pargs.remove = args->remove;
        pargs.confirm = args->confirm;

        map_process_entries( map, compare_collected_entries, &pargs );
        printf( "Found %ld instances of redundant files, for a total of %ld redundant files\n",
                pargs.instances, pargs.total );
        if ( pargs.remove ) {
            printf( "Removed %ld redundant files\n", pargs.removed );
        }
    }
#if TIME_MEASURE
    printf( "Time elapsed processing files %ld milliseconds\n", get_nanosecond_timestamp() - file_process_start );
    printf( "  Time spent in file comparison: %ld nanoseconds\n", file_compare_spent );
    printf( "  Time spent in file stat:       %ld nanoseconds\n", file_stat_spent );
#endif
}

typedef struct {
    map_t           *map;
    unsigned long   count;
    bool            zero;
    bool            absent;
    bool            compare;
    bool            remove;
    bool            confirm;
} map_context_t;

#if TIME_MEASURE
static int64_t slice_append_spent = 0;
static int64_t slice_creation_spent =0;
#endif

#define MIN_SLICE_SIZE   4
static int build_map( char *path, size_t size, void *context )
{
    map_context_t *mcp = context;

    if ( 0 != size ) {  // skip empty files as a map key cannot be 0
        ++mcp->count;
        slice_t *paths = (slice_t *)map_lookup_entry( mcp->map, (void *)size );
        if ( NULL == paths ) {
#if TIME_MEASURE
            int64_t before_creation = get_nanosecond_timestamp( );
#endif
            paths = new_slice( sizeof( char * ), MIN_SLICE_SIZE );
#if TIME_MEASURE
            slice_creation_spent += get_nanosecond_timestamp( ) - before_creation;
#endif
            if ( NULL != paths ) {
                map_insert_entry( mcp->map, (void *)size, (void*)paths );
            } // else it fails just below
        }
        // append path to existing paths slice,
        // it fails if slice is NULL or can't be appended to
#if TIME_MEASURE
        int64_t before_append = get_nanosecond_timestamp( );
#endif
        if ( 0 != pointer_slice_append_item( paths, path ) ) {
            printf( "Out of memory while building map" );
            return -1;
        }
#if TIME_MEASURE
        slice_append_spent += get_nanosecond_timestamp( ) - before_append;
#endif
        // do not free path here as it is the value in the
        // hash table (while size is the key)
        return 1;
    }
    if ( mcp->zero ) {
        printf( "Empty file %s\n", path );
    }
    return 0;
}

#define INITIAL_HASH_SIZE   2048
#define MAX_COLLISIONS      10

extern map_t *collect_same_size_files( args_t *args )
{
    // By default start with a medium size map table.
    // The collision threshold is very high on purpose, because we are
    // creating a collision for each file with the same size (in addition
    // to the actual collisions that a modulo can create).
    map_t *map = new_map( NULL, NULL, INITIAL_HASH_SIZE, MAX_COLLISIONS );
    if ( NULL == map ) {
        return NULL;
    }

    map_context_t ctxt;
    ctxt.map = map;
    ctxt.count = 0;
#if TIME_MEASURE
    int64_t start = get_nanosecond_timestamp( );
#endif
    for ( search_t *sptr = args->paths; NULL != sptr->path; ++sptr ) {
        ctxt.zero = sptr->zero;
        traverse_directory( sptr->path, sptr->nosub, build_map, &ctxt );
    }
#if TIME_MEASURE
    int64_t stop = get_nanosecond_timestamp( );
    printf( "Time elapsed building map: %ld milliseconds\n", NANOSEC_TO_MILLISEC(stop-start) );
    printf( "  Time spent in slice creation:  %ld nanoseconds\n", slice_creation_spent );
    printf( "  Time spent in slice appending: %ld nanoseconds\n", slice_creation_spent );
    printf( "  Time spend in file stat:       %ld nanoseconds\n", file_stat_spent );
#endif
    printf( "Traversed %ld files\n", ctxt.count );
    return map;
}

static bool free_entry( uint32_t index,
                        const void *key, const void *data, void *ctxt )
{
    (void)index;
    (void)key;
    (void)ctxt;
    pointer_slice_free( (slice_t *)data );
    return false;
}

extern void free_collected_data( map_t *map )
{
    map_process_entries( map, free_entry, NULL );
    map_free( map );
}
