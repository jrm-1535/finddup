
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include <stdbool.h>
#include "comp.h"

void help( void )
{
    printf( "fmis -h -nz <target-path> [[-nz] <path>]*\n\n" );
    printf( "look for a target file or for files in the target directory whose\n" );
    printf( "content cannot be found in any following path directories or their\n" );
    printf( "sub-directories, regardless their actual file names.\n\n" );
    printf( "Options:\n" );
    printf( "   -h          print this help message and exit.\n" );
    printf( "   -n          do not enter subdirectories. This option applies only\n" );
    printf( "               to the following path and may be repeated before each\n" );
    printf( "               directory path to search\n" );
    printf( "   -N          same as -n but it applies to all following paths\n" );
    printf( "   -z          show empty files while traversing directories. By\n" );
    printf( "               defaut ignore empty files. This option applies only\n" );
    printf( "               to the following path and may be repeated before each\n" );
    printf( "               path to search\n" );
    printf( "   -Z          same as -z but it applies to all following paths\n\n" );

    printf( "The <target-path> can be a single file path or a directory of files to\n" );
    printf( "look for in the following paths. The following paths are the pathnames\n" );
    printf( "of the directories to scan for identical files. If no path are given\n" );
    printf( "after the mandatory <target-path>, the current directory is used instead.\n\n" );
}

void get_args( int argc, char **argv, args_t *args )
{
    args->paths = NULL;
    args->target = NULL;
    args->compare = true;
    args->remove = false;
    args->confirm = false;

    bool nosub_default = false;
    bool nosub = false;
    bool zero_default = false;
    bool zero = false;
    int n_paths = 0;
    bool expecting_target = true;

    for ( int i = 1; i < argc; ++i ) {
        char *arg = argv[i];
        switch (*arg) {
        case '-':
            for ( int j = 1; 0 != arg[j]; ++j) {
                switch( arg[j] ) {
                case 'h': case 'H':
                    help();
                    exit(0);
                case 'N':
                    nosub = nosub_default = true;
                    break;
                case 'n':
                    nosub = true;
                    break;
                case 'Z':
                    zero = zero_default = true;
                    break;
                case 'z':
                    zero = true;
                    break;
                default:
                    printf( "-%c: ", arg[j] );
                    error( "unrecognized option" );
                }
            }
            break;

        default:
            if ( expecting_target ) {
                set_new_target( args, arg, nosub, zero );
                nosub = nosub_default;
                zero = zero_default;
                expecting_target = false;
                break;
            }
            // worse case all following arguments are directory paths
            if ( NULL ==  args->paths ) {
                args->paths = new_paths(1 + argc - i);  // +NULL to end the list
                n_paths = 0;                            // index in paths
            }
            set_path( args, n_paths, argv[i], nosub, zero );
            nosub = nosub_default;
            zero = zero_default;
            ++n_paths;
            break;
        }
    }
    if ( NULL == args->paths ) {
        args->paths = new_paths(2);
        set_path( args, 0, getcwd( NULL, 4096 ), nosub, zero );
        set_path( args, 1, NULL, false, false );
    }
}

int main( int argc, char**argv )
{
    args_t args;
    get_args( argc, argv, &args );
#if 0
    printf( "Target: %s nosub=%d, zero=%d paths:\n",
            args.target->path, args.target->nosub, args.target->zero );
    for ( int i = 0; ; ++i ) {
        if ( NULL == args.paths[i].path ) {
            break;
        }
        printf( " name %s nsub=%d, zero=%d\n",
                args.paths[i].path, args.paths[i].nosub, args.paths[i].zero );
    }
#endif

    map_t *map = collect_same_size_files( &args );
    search_targets( map, &args );
    free_collected_data( map );
    free_target_n_paths( &args );
}
