#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#include "mpi.h"

void serialSolve(int *inArr, int *outArr, int size) { // From coursework sheet
    /* [ ======================================================== ] */
    /* [ ===================== Serial Solve ===================== ] */
    /* [ ======================================================== ] */
    // Single threaded solve of the running total
    outArr[0] = inArr[0]; // Initial value
    for (int i = 1; i < size; ++i) outArr[i] = outArr[i - 1] + inArr[i]; // Calculate running total from prev item
}

int main(int argc, char *argv[]) {
    /* [ ======================================================== ] */
    /* [ ===================== Setup Phase ====================== ] */
    /* [ ======================================================== ] */
    // Initial variables
    int rankID, rankSize; // rankID is which process rankSize for total processes - must be a power of 2
    int inputSize, arraySize; // size of wanted array and used array with padding

    int *genArray; // Array pointer on each process for generated array
    int *parArray; // Array pointer on each process for parallel solved array
    int *serArray; // Array pointer on each process for serially solved array

    // MPI Variables
    MPI_Init(&argc, &argv); // Initialise MPI
    MPI_Comm_rank(MPI_COMM_WORLD, &rankID); // Saves for each rank:
    MPI_Comm_size(MPI_COMM_WORLD, &rankSize); // what rank and how many

    const int root = 0; // First process chosen as root for all serial tasks

    // Timer variables
    clock_t start, end; // clock variables for timing
    double parTime, serTime; // create parallel timer and serial timer

        /* [ ======================================================== ] */
        /* [ ================ Process Checking Phase ================ ] */
        /* [ ======================================================== ] */
    if (rankID == root) { // Root node for serial work
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
        /* [ ======================================================== ] */
        /* [ ===================== Input Phase ====================== ] */
        /* [ ======================================================== ] */
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
        printf("\nInput array size: %d\n", inputSize);
        fflush(stdout);

        /* [ ======================================================== ] */
        /* [ =================== Generation Phase =================== ] */
        /* [ ======================================================== ] */
        arraySize = 2;
        while (arraySize < inputSize || arraySize < rankSize) { // Find usable array size
            arraySize *= 2; // Array must be multiple of 2
        }

        printf("Padded array size: %d\n", arraySize);
        fflush(stdout);

        genArray = (int *) calloc(arraySize, sizeof(int)); // Generate padded size with 0's

        for (size_t i = 0; i < inputSize; ++i) {
            //genArray[i] = rand() % 50; // Generate random numbers for the wanted array size, keeping 0 padding
            genArray[i] = i+1;
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

    MPI_Bcast(&arraySize, 1, MPI_INT, root, MPI_COMM_WORLD); // Broadcast inputSize to all nodes

    MPI_Barrier(MPI_COMM_WORLD); // Wait for root node and sync

    /* [ ======================================================== ] */
    /* [ ================== Distribution Phase ================== ] */
    /* [ ======================================================== ] */
    int chunkSize = arraySize / rankSize; // Chunk size - number of array points per process
    int *chunk = calloc(chunkSize, sizeof(int)); // Allocate memory for chunks

    // Distribute array, given powers of 2 and padding, each process should have equal array chunks
    MPI_Scatter(genArray, chunkSize, MPI_INT, chunk, chunkSize, MPI_INT, root, MPI_COMM_WORLD); // distribute array

    /* [ ======================================================== ] */
    /* [ ==================== Parallel Solve ==================== ] */
    /* [ ======================================================== ] */
    // The algorithm for calculating the running total in parallel

    // Variables required
    int step = 1; // iterative step for up phase
    int minor = step; // distance from step value to wanted value, half of step
    int sendProc, recvProc, sendChnk, recvChnk; // Send and receive for process and chunk
    // Identifies processes and chunks to be moved

    start = clock(); // Start timer for parallel solve

    /* [ ======================================================== ] */
    /* [ ======================= Up Phase ======================= ] */
    /* [ ======================================================== ] */
    // Upward totals of increasing stride sizes
    while (step < chunkSize) { // performed on arrays per process, no memory movement
        minor = step; // previous step size moving up
        step *= 2; // double step size each time

        for (int i = step - 1; i < chunkSize; i += step) { // For all values of the array upwards
            //printf("process %d adding chunk %d to chunk %d\n", rankID, i, i - minor);
            chunk[i] += chunk[i - minor]; // Running total
        }
    }

    //*/
    while (step < arraySize) { // performed on the rest of the array, with memory movement
        minor = step; // previous step size moving up
        step *= 2; // double step size each time

        for (int i = step - 1; i < arraySize; i += step) {
            sendProc = (i - minor) / chunkSize; // find sending process
            sendChnk = (i - minor) % chunkSize; // find sending chunk
            recvProc = i / chunkSize; // find receiving process
            recvChnk = i % chunkSize; // find receiving chunk

            if (rankID == sendProc) { // if only sending process
                MPI_Send(&chunk[sendChnk], 1, MPI_INT, recvProc, 0, MPI_COMM_WORLD);

            } else if (rankID == recvProc) { // if only receiving process
                int receive = 0;
                MPI_Recv(&receive, 1, MPI_INT, sendProc, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                chunk[recvChnk] += receive; // Add received value
            } // end if
        } // end for
    } // exit while when step is as large as arraySize

    /* [ ======================================================== ] */
    /* [ ====================== Down Phase ====================== ] */
    /* [ ======================================================== ] */
    // Downward stride totals for missing values of decreasing stride sizes

    while (step > 2) { // Minimum barrier for decreasing strides
        step = minor; // moving down
        minor = step / 2; // minor is half of 2

        for (int i = step - 1 + minor; i < arraySize - 1; i += step) {
            sendProc = (i - minor) / chunkSize; // find sending process
            sendChnk = (i - minor) % chunkSize; // find sending chunk
            recvProc = i / chunkSize; // find receiving process
            recvChnk = i % chunkSize; // find receiving chunk

            if (rankID == sendProc && rankID == recvProc ) { // if send and receive are the same process
                chunk[recvChnk] += chunk[sendChnk]; // no messaging, add within chunk

            } else if (rankID == sendProc) { // if only sending process
                MPI_Send(&chunk[sendChnk], 1, MPI_INT, recvProc, 0, MPI_COMM_WORLD);

            } else if (rankID == recvProc) { // if only receiving process
                int receive = 0;
                MPI_Recv(&receive, 1, MPI_INT, sendProc, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                chunk[recvChnk] += receive; // Add received value
            } // end if
        } // end for
    } // end while when step is smaller than minimum size

    end = clock();
    parTime = ((double) (end - start)) / CLOCKS_PER_SEC; // Calculate time taken

    MPI_Barrier(MPI_COMM_WORLD);

    /* [ ======================================================== ] */
    /* [ ===================== Return Phase ===================== ] */
    /* [ ======================================================== ] */
    // allocate space for output array
    if (rankID == root) parArray = calloc(arraySize, sizeof(int));

    // Return all individual data to output array
    MPI_Gather(chunk, chunkSize, MPI_INT, parArray, chunkSize, MPI_INT, root, MPI_COMM_WORLD);

    /* [ ======================================================== ] */
    /* [ ===================== Serial Solve ===================== ] */
    /* [ ======================================================== ] */
    // Serial calculation for running total
    start = clock();

    if (rankID == root) {
        serArray = calloc(arraySize, sizeof(int));

        serialSolve(genArray, serArray, arraySize);
    }

    end = clock();
    serTime = ((double) (end - start)) / CLOCKS_PER_SEC; // Calculate time taken

    /* [ ======================================================== ] */
    /* [ =================== Comparison Phase =================== ] */
    /* [ ======================================================== ] */
    if (rankID == root) { // Parallel no longer needed

        // Output Timings and Arrays
        printf("\nParallel calculation took: %fs", parTime);
        printf("\nSerial calculation took: %fs", serTime);
        printf("\nParallel Array: [ ");
        for (size_t i = 0; i < arraySize; ++i) {
            printf("%d ", parArray[i]);
        }
        printf("]\nSerial Array: [ ");
        for (size_t i = 0; i < arraySize; ++i) {
            printf("%d ", serArray[i]);
        }
        printf("]\n");

        // Check validity
        int check = 1;
        for (int i = 0; i < arraySize; ++i) {
            if (serArray[i] != parArray[i]) {
                check = 0;
                break;
            } else {
            }
        }
        if (check == 1) printf("\nOutputs match, success!\n");
        else
            printf("\nOutputs do not match, failed!\n");

    }

    /* [ ======================================================== ] */
    /* [ ==================== Clean-up Phase ==================== ] */
    /* [ ======================================================== ] */

    if (rankID == root) free(genArray);
    free(chunk);

    // Clean up of processes
    MPI_Finalize();
    return 0;

} // EOF

