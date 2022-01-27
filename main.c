/*
 *  This program compares the output of a cpp program with the text provided in another file. Intended to aid in debugging vocareum test cases for matching output.
 *  The first character that doesn't match the correct output will be highlighted in red along with all preceeding characters.
 *  Usage: ./autocompare [executable] -test_cases [input file] [output file] -num [(optional) number of test cases, default=1]
 *  NOTE: The input/output files can be specified with a '*' to denote the test case number.
 *  IMPORTANT: The '' or "" surrounding the * are necessary. 
 *  Example: ./autocompare a.out -test_cases case'*' case'*'.correct -num 6
 *  This will run a.out with the contents of case[1-6] and compare the program output with the content of case[1-6].correct
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
//#include <sys/wait.h>
#include <sys/prctl.h>
#include <signal.h>

// Terminal Ansi Color Codes
#define TERM_GREEN     		"\033[32m"
#define TERM_RED       		"\033[31m"
#define TERM_YELLOW    		"\033[33m"
#define TERM_BLUE      		"\033[34m"
#define TERM_CYAN			"\033[36m"

#define TERM_BOLD      		"\033[1m"
#define TERM_BOLD_OFF  		"\033[21m"
#define TERM_UNDERLINE		"\033[4m"
#define TERM_UNDERLINE_OFF 	"\033[24m"

#define TERM_DEFAULT   		"\033[0m"

typedef struct {
    uint32_t len;
    char *buffer;
} FileBuffer;

FileBuffer readEntireFile( FILE *fp );

typedef struct {
    int fileCount;
    FILE **inputFiles;
    FILE **outputFiles;
} TestCases;

TestCases openTestCaseFiles( int argc, char **argv );
void closeTestCaseFiles( TestCases *testCases );

// str1 is actual, and str2 is correct
int compareAndPrint( char *str1, char *str2 );
void printTestResults( int *passedCases, int count );

int main( int argc, char **argv ) {
    TestCases testCases = openTestCaseFiles( argc, argv );
	int *casesPassed = calloc( testCases.fileCount, sizeof(int) );
	
    char buffer[256];

    for( int i = 0; i < testCases.fileCount; ++i ) {
        printf( "%s--------------------------------------------\nRunning Test %s%d%s\n--------------------------------------------\n%s", TERM_BLUE, TERM_YELLOW, i+1, TERM_BLUE, TERM_DEFAULT );
        memset( buffer, 0, 256 );

		pid_t pid = 0;
		int inpipefd[2];
		int outpipefd[2];
		int status;

		pipe(inpipefd);
		pipe(outpipefd);
		pid = fork();
		if( pid == 0 ) { // Child
			dup2( outpipefd[0], STDIN_FILENO );
		    dup2( inpipefd[1], STDOUT_FILENO );
		    dup2( inpipefd[1], STDERR_FILENO );

	        // Ask kernel to deliver SIGTERM in case the parent dies
		    prctl( PR_SET_PDEATHSIG, SIGTERM );

		    execl( argv[1], (char*) NULL );
		    exit(1); // Should not be executed unless execl function was not successfull
		}

		// Close unused pipe ends
		// Only need outpipefd[1] to write to child and inpipefd[0] to read from child
		close( outpipefd[0] );
		close( inpipefd[1] );

		FileBuffer inputFile = readEntireFile( testCases.inputFiles[i] );
	    FileBuffer outputFile = readEntireFile( testCases.outputFiles[i] );

		write( outpipefd[1], inputFile.buffer, inputFile.len );
		read( inpipefd[0], buffer, 256 );
	
		if( compareAndPrint( buffer, outputFile.buffer ) == -1 ) {
			casesPassed[i] = 1;
		}

        free( inputFile.buffer );
		free( outputFile.buffer );

		printf( TERM_GREEN );
		//printf( "%s", buffer );
		printf( TERM_DEFAULT );

		kill( pid, SIGKILL ); // Send SIGKILL signal to the child process
		waitpid( pid, &status, 0 );
    }
	
	printTestResults( casesPassed, testCases.fileCount );

    closeTestCaseFiles( &testCases );
	free( casesPassed );
    return 0;
}

FileBuffer readEntireFile( FILE *fp ) {
    FileBuffer fileBuffer = {0};

    fseek( fp, 0L, SEEK_END );
    fileBuffer.len = ftell( fp );
    fseek( fp, 0L, SEEK_SET );

    fileBuffer.buffer = calloc( fileBuffer.len+1, sizeof(char) );
    fread( fileBuffer.buffer, 1, fileBuffer.len, fp );    

    return fileBuffer;
}

TestCases openTestCaseFiles( int argc, char **argv ) {
    int testCaseArgIndex, testCasesFound = 0;
    for( testCaseArgIndex = 2; testCaseArgIndex < argc; ++testCaseArgIndex ) {
	if( strcmp( argv[testCaseArgIndex], "-test_cases" ) == 0 ) {
	    testCasesFound = 1;
	    break;
        }
    }

    if( !testCasesFound ) {
	printf( "The command line argument -test_cases followed by two files is required\n" );
	exit( 0 );
    }

    TestCases testCases = {0};
    testCases.fileCount = 1;

    for( int j = 0; j < argc; ++j ) {
	if( strcmp( argv[j], "-num" ) == 0 ) {
    	    if( ++j < argc ) {
		testCases.fileCount = atoi( argv[j] );
	 	break;
	    }
	}
    }

    if( testCases.fileCount == 0 ) {
	testCases.fileCount = 1;
    }

    int inputFileFormatCount = strlen( argv[testCaseArgIndex+1] ) + 1;
    int outputFileFormatCount = strlen( argv[testCaseArgIndex+2] ) + 1;
    char *inputFileFormat = calloc( inputFileFormatCount, sizeof(char) );
    char *outputFileFormat = calloc( outputFileFormatCount, sizeof(char) );
    strcpy( inputFileFormat, argv[testCaseArgIndex+1] );
    strcpy( outputFileFormat, argv[testCaseArgIndex+2] );

    testCases.inputFiles = calloc( testCases.fileCount, sizeof(FILE*) );
    testCases.outputFiles = calloc( testCases.fileCount, sizeof(FILE*) );

    int tempFilenameBufferCount = inputFileFormatCount > outputFileFormatCount ? inputFileFormatCount : outputFileFormatCount; 
    tempFilenameBufferCount += 5;

    // Split filename between * if present
    char *inputFileFirstSection;
    char *inputFileLastSection;
    int delimiterFound = 0;

    for( int i = 0; inputFileFormat[i] != 0; ++i ) {
	if( inputFileFormat[i] == '*' ) {
	    delimiterFound = 1;
	    inputFileFirstSection = calloc( i + 1, sizeof(char) );
            strncpy( inputFileFirstSection, inputFileFormat, i );
            inputFileLastSection = calloc( inputFileFormatCount - i, sizeof(char) );
            strncpy( inputFileLastSection, &inputFileFormat[i+1], inputFileFormatCount - i );
	    break;
	}
    }

    if( !delimiterFound ) {
   	inputFileFirstSection = calloc( inputFileFormatCount, sizeof(char) );
        strcpy( inputFileFirstSection, inputFileFormat );
        inputFileLastSection = calloc( 1, sizeof(char) );
    }

    char *outputFileFirstSection;
    char *outputFileLastSection;
    delimiterFound = 0;

    for( int i = 0; outputFileFormat[i] != 0; ++i ) {
	if( outputFileFormat[i] == '*' ) {
            delimiterFound = 1;
	    outputFileFirstSection = calloc( i + 1, sizeof(char) );
            strncpy( outputFileFirstSection, outputFileFormat, i );
            outputFileLastSection = calloc( outputFileFormatCount - i, sizeof(char) );
            strncpy( outputFileLastSection, &outputFileFormat[i+1], outputFileFormatCount - i );
	    break;
	}
    } 

    if( !delimiterFound ) {
   	outputFileFirstSection = calloc( outputFileFormatCount, sizeof(char) );
        strcpy( outputFileFirstSection, outputFileFormat );
        outputFileLastSection = calloc( 1, sizeof(char) );
    }

    free( inputFileFormat );
    free( outputFileFormat );

    char *tempFilenameBuffer = malloc( tempFilenameBufferCount * sizeof(char) );

    for( int j = 0; j < testCases.fileCount; ++j ) {
        memset( tempFilenameBuffer, 0, tempFilenameBufferCount );
        sprintf( tempFilenameBuffer, "%s%d%s", inputFileFirstSection, j+1, inputFileLastSection );        
	testCases.inputFiles[j] = fopen( tempFilenameBuffer, "r" );
        if( !testCases.inputFiles[j] ) {
            printf( "Could not open file %s\nCheck if file arguments are correct\n", tempFilenameBuffer );
            closeTestCaseFiles( &testCases );
	    exit(0);
	} else {
	    #ifdef DEBUG
 	    printf( "Opening file %s\n", tempFilenameBuffer );
	    #endif
	}

        memset( tempFilenameBuffer, 0, tempFilenameBufferCount );
        sprintf( tempFilenameBuffer, "%s%d%s", outputFileFirstSection, j+1, outputFileLastSection );        
        testCases.outputFiles[j] = fopen( tempFilenameBuffer, "r" );
        if( !testCases.outputFiles[j] ) {
            printf( "Could not open file %s\nCheck if file arguments are correct\n", tempFilenameBuffer );
	    closeTestCaseFiles( &testCases );
            exit(0);
	} else {
            #ifdef DEBUG
	    printf( "Opening file %s\n", tempFilenameBuffer );
	    #endif
	}
    }

    free( inputFileFirstSection );
    free( inputFileLastSection );
    free( outputFileFirstSection );
    free( outputFileLastSection );

    return testCases;
}

void closeTestCaseFiles( TestCases *testCases ) {
    for( int i = 0; i < testCases->fileCount; ++i ) {
	if( testCases->inputFiles[i] ) {
            fclose( testCases->inputFiles[i] );
	}
	if( testCases->outputFiles[i] ) {
	   fclose( testCases->outputFiles[i] );
	}
    }
}

int compareAndPrint( char *str1, char *str2 ) {
	int firstBadCharacter = -1;
    int printIndex;

    printf( TERM_GREEN );

    for( printIndex = 0; str1[printIndex] == str2[printIndex] && str1[printIndex] != 0 && str2[printIndex] != 0; ++printIndex );

    int str1Len = strlen( str1 );
	int str2Len = strlen( str2 );   

    if( printIndex == str1Len && printIndex == str2Len ) {
		printf( "%s", str1 );
		printf( TERM_DEFAULT );
		return firstBadCharacter;
    }

	firstBadCharacter = printIndex;

    char *goodCharBuffer = calloc( printIndex+1, sizeof(char) );
	strncpy( goodCharBuffer, str1, printIndex );
    printf( "%s", goodCharBuffer );

	char *badCharBuffer = calloc( str1Len - printIndex + 1, sizeof(char) );
    strcpy( badCharBuffer, &str1[printIndex] );
    printf( TERM_RED );
	printf( "%s", badCharBuffer );

	free( goodCharBuffer );
	free( badCharBuffer );

    printf( TERM_YELLOW );
	printf( TERM_BOLD );

	printf( "--------------------------------------------\n" );

    if( str1Len < str2Len ) {
	    printf( "Correct output is longer than actual output\n" );
	} else {
	    printf( "Correct output is shorter than actual output\n" );
	}

    if( str1Len > printIndex ) {
		switch( str1[printIndex] ) {
		 	case '\n': 
				printf( "Check for extra newlines in your output\n" );
				break;
			case ' ':
				printf( "Check for extra spaces in your output\n" );
				break;
			default:
				break;
		}	
	}

	if( str2Len > printIndex ) {
		switch( str2[printIndex] ) {
			case '\n':			
				printf( "Your output might be missing a newline\n" );
				break;
			case ' ':
				printf( "Your output might be missing a space\n" );
				break;
			default:
				break;
		}
	}

    printf( TERM_DEFAULT );
  
	return firstBadCharacter;
}

void printTestResults( int *passedCases, int count ) {
	printf( "%s%s", TERM_BOLD, TERM_CYAN );
	printf( "--------------------------------------------\nSUMMARY:\n" );
	for( int i = 0; i < count; ++i ) {
		printf("RESULT: case%d [%d/1]\n", i+1, passedCases[i] );
	}
	printf( TERM_DEFAULT );
}
