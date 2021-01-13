#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#include "mpi.h"

void serialSolve(int *inArr, int *outArr, int size) {
    // Single threaded solve of the running total
    outArr[0] = inArr[0];

    for (int i = 1; i < size; ++i)
        inArr[i] = outArr[i - 1] + inArr[i];
}

int main(int argc, char *argv[]) {

    /* [ ================= Setup Phase ================= ] */

    // Initial variables
    int rankID, rankSize; // rankID is which process rankSize for total processes - must be a power of 2
    int inputSize, arraySize; // size of wanted array and used array with padding

    // MPI Variables
    MPI_Init(&argc, &argv); // Initialise MPI
    MPI_Comm_rank(MPI_COMM_WORLD, &rankID); // Saves for each rank:
    MPI_Comm_size(MPI_COMM_WORLD, &rankSize); // what rank and how many

    const int root = 0; // First process chosen as root for all serial tasks

    // Timer variables
    clock_t start, end; // clock variables for timing
    double parTime, serTime; // create parallel timer and serial timer

    if (rankID == root) { // Root node for serial work

        /* [ ================= Process Checking Phase ================= ] */

        /* If number of processes is not a power of 2, then the maths will break */
        int pow2 = 2; // power of 2
        while (pow2 < rankSize) {
            pow2 *= 2;
        }
        if (pow2 != rankSize) {
            printf("\nSorry, processor count must be a power of 2.\n");
            fflush(stdout); // Force display buffer

            MPI_Abort(MPI_COMM_WORLD, -2);
            MPI_Finalize(); // Clean up all processes

            exit(-2); // Exit with error
        }

        /* [ ================= Input Phase ================= ] */

        printf("\nInput a size for the Array [int]: ");
        fflush(stdout); // Force display buffer
        bool reject = 1;

        while (reject == 1) { // Simple loop to ensure valid input
            if (!scanf("%d", &inputSize)) {
                scanf("%*[^\n]"); //discard that line up to the newline
                printf("Could not read integer value, try again: ");
                fflush(stdout);
                reject = 1;
            } else {
                reject = 0;
            }
        }
        printf("\nsize %d\n", inputSize);
        fflush(stdout);
    }// end root node operation

    /* [ ================= Generate Phase ================= ] */

    int *genArray; // Array pointer on each process

    if (rankID == root) { // Root node for serial work

        arraySize = 2;
        while (arraySize < inputSize || arraySize < rankSize) { // Find usable array size
            arraySize *= 2; // Array must be multiple of 2
        }

        MPI_Bcast(&arraySize, 1, MPI_INT, root, MPI_COMM_WORLD); // Broadcast inputSize to all nodes

        printf("padded array size: %d\n", arraySize);
        fflush(stdout);

        genArray = (int *) calloc(arraySize, sizeof(int)); // Generate padded size with 0's

        for (size_t i = 0; i < inputSize; ++i) {
            genArray[i] = rand(); // Generate random numbers for the wanted array size, keeping 0 padding
        }

        //*/ Debug: Output array
        printf("\nArray Values: [ ");
        for (size_t i = 0; i < arraySize; ++i) {
            printf("%d ", genArray[i]);
        }
        printf("]\n");
        fflush(stdout);
        //*/
    } // end root node operation

    MPI_Barrier(MPI_COMM_WORLD); // Wait for root node and sync

    /* [ ================= Distribution Phase ================= ] */

    int chunkSize = arraySize / rankSize; // Chunk size - number of array points per process
    int *chunk = calloc(chunkSize, sizeof(int)); // Allocate memory for chunks
    // Distribute array, given powers of 2 and padding, each process should have equal array chunks
    MPI_Scatter(genArray, chunkSize, MPI_INT, chunk, chunkSize, MPI_INT, root, MPI_COMM_WORLD); // distribute array

    /* [ ================= Prefix Scan ================= ] */
    // The algorithm for calculating the running total in parallel

    // Variables required
    int step = 1; // iterative step for up phase
    int minor; // distance from step value to wanted value, half of step
    int sendProc, recvProc, sendChnk, recvChnk; // Send and receive
    // Identifies processes and chunks to be moved

    start = clock(); // Start timer for parallel solve

    /* [ ================= Up Phase ================= ] */

    while (step < chunkSize) { // performed on arrays per process, no memory movement
        minor = step; // previous step size moving up
        step *= 2; // double step size each time

        for (int i = step - 1; i < chunkSize; i += step) { // For all values of the array upwards
            chunk[i] += chunk[i - minor]; // Running total
        }
    }
    while (step < arraySize) { // performed on the rest of the array, with memory movement
        minor = step; // previous step size moving up
        step *= 2; // double step size each time

        for (int i = step - 1; i < arraySize; i += step) {
            sendProc = i - minor / chunkSize; // find sending process
            recvProc = i / chunkSize; // find receiving process

            if (rankID == sendProc) { // only sending process
                sendChnk = (i - minor) % chunkSize;

                MPI_Send( &chunk[sendChnk], 1, MPI_INT, recvProc, 0, MPI_COMM_WORLD);

            } else if (rankID == recvProc) { // only receiving process
                recvChnk = i % chunkSize;

                int receive = 0;
                MPI_Recv( &receive, 1, MPI_INT, sendProc, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                chunk[recvChnk] += receive;
            } // end if
        } // end for
    } // exit while when step is as large as arraySize

    /* [ ================= Down Phase ================= ] */

    while ( step >= chunkSize ) {
        step = minor; // moving down powers of 2
        minor /= 2; // minor leads

        for (int i = step - 1 + minor; i < arraySize; i += step) {
            sendProc = i - minor / chunkSize;
            recvProc = i / chunkSize;

            if (rankID == sendProc) { // only sending process
                sendChnk = (i - minor) % chunkSize;

                MPI_Send( &chunk[sendChnk], 1, MPI_INT, recvProc, 0, MPI_COMM_WORLD);

            } else if (rankID == recvProc) { // only receiving process
                recvChnk = i % chunkSize;

                int receive = 0;
                MPI_Recv( &receive, 1, MPI_INT, sendProc, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                chunk[recvChnk] += receive;
            } // end if
        } // end for
    } // end while when step is smaller than chunkSize

    while ( step > 2 ) { // perform down phase calculation per chunk if applicable
        step = minor;
        minor /= 2;

        for (int i = step - 1 + minor; i < arraySize; i += step) {
            chunk[i] += chunk[i - minor];
        } // end for
    } // end at step < 2, minimum step size met









    end = clock();
    parTime = ((double) (end - start)) / CLOCKS_PER_SEC; // Calculate time taken

    /* [ ================= Running Total ================= ] */
    // Serial calculation for running total

    start = clock();

    /* [ ================= Serial Output ================= ] */

    if (rankID == root) {


    }


    end = clock();
    serTime = ((double) (end - start)) / CLOCKS_PER_SEC; // Calculate time taken

    /* [ ================= Comparisons and Results ================= ] */



    /* [ ================= Cleanup ================= ] */

    // check end of file
    printf("EOF ");

    if (rankID == root) free(genArray);
    free(chunk);


    // Clean up of processes
    MPI_Finalize();
    return 0;
}
