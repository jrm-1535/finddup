
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "comp.h"

void help( void )
{
    printf( "fdup -h -c -r -i -ic -a path\n\n" );
    printf( "look for multiple instances of the same file content in a\n" );
    printf( "directory and its sub-directories\n\n" );
    printf( "Options:\n" );
    printf( "   -h  print this help message and exit.\n" );
    printf( "   -c  compare file contents. By default, check only if file\n");
    printf( "       size are the same.\n" );
    printf( "   -r  remove some of the same files. By default, just list\n" );
    printf( "       their names.\n" );
    printf( "   -i  interactive removal: ask which file(s) to remove, if\n" );
    printf( "       any. This is the default for -r\n" );
    printf( "   -ic interactive removal with extra confirmation\n" );
    printf( "   -a  automatic removal of all identical files but one\n\n" );

    printf( "path is the pathname of the root directory to scan for identical\n" );
    printf( "files. If path is absent, the current directory is used instead.\n\n" );
    printf( "Notes:\n" );
    printf( "   With no option selected, fdup displays the path of all files\n" );
    printf( "   with the same size. To check if file contents are truly\n" );
    printf( "   identical, select the option -c.\n\n" );
    printf( "   Options -r, -i, -ic and -a are ignored if option -c is not\n" );
    printf( "   selected.\n\n" );
    printf( "   Option -r tries to remove some identical files, depending on\n" );
    printf( "   options -i, -ic or -a:\n\n" );
    printf( "     - Option -i allows selection among a list of identical files\n" );
    printf( "       of which file(s) to remove, if any. It is the default if\n" );
    printf( "       -r is selected and none of -ic or -a is selected.\n" );
    printf( "     - Option -ic requires a confirmation after the selection of\n" );
    printf( "       file(s) to remove.\n" );
    printf( "     - Option -a automatically removes all files but the one with\n" );
    printf( "       the shortest name. This removes files possibly in multiple\n" );
    printf( "       sub-directories\n\n");
    printf( "   Options -i, -ic and -a are ignored if option -r is not selected.\n" );
    printf( "   Options -a and -i or -ic are exclusive.\n\n" );
}

void error( char *msg )
{
    printf( "%s\n", msg );
    exit(1);
}

typedef struct {
    char    *path;
    bool    compare, remove, interactive, confirm;
} args_t;

void get_args( int argc, char **argv, args_t *args )
{
    args->path = NULL;
    args->compare = false;
    args->remove = false;
    args->interactive = false;
    args->confirm = false;
    bool automatic = false;

    for ( int i = 1; i < argc; ++i ) {
        char *arg = argv[i];
        switch (*arg) {
        case '-':
            switch (arg[1]) {
            case 'h': case 'H':
                help();
                exit(0);
            case 'c': case 'C':
                args->compare = true;
                break;
            case 'r': case 'R':
                args->remove = true;
                break;
            case 'i': case 'I':
                args->interactive = true;
                if ( arg[2] == 'c' || arg[2] == 'C' )
                    args->confirm = true;
                break;
            case 'a': case 'A':
                automatic = true;
                break;
            default:
                error( "unrecognized option" );
            }
            break;
        default:
            if ( args->path ) {
                error( "multiple start path names");
            }
            args->path = arg;
            break;
        }
    }
    if ( args->compare == false ) {
        args->remove = false;
    }
    if ( args->remove == false ) {
        args->interactive = false;
        args->confirm = false;
    } else if ( automatic == false ) {
        args->interactive = true;
    } else {
        args->interactive = false;
        args->confirm = false;
        error( "automatic removal is not yet implemented" );
    }
}

int main( int argc, char**argv )
{
    args_t  args;
    get_args( argc, argv, &args );

//    printf( "compare %d remove %d, interactive %d, confirm %d\n",
//            args.compare, args.remove, args.interactive, args.confirm );
    if (args.path) {
        printf( "root: %s\n", args.path );
    }

    if ( NULL != args.path ) {
        char *path = args.path;
        if ( 0 != chdir( path ) )
            printf( "lsame: unable to change directory to %s\n", path );
    }

    char *cur_path = getcwd( NULL, 4096 );
    printf("List same files started at directory:%s\n", cur_path );
    map_t *map = collect_similar_files( cur_path );
    free( cur_path);

//    map_stats( ht );

    check_files( map, args.compare, args.remove, args.interactive, args.confirm );

    free_collected_data( map );
}
