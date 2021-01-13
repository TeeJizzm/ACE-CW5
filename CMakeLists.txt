cmake_minimum_required(VERSION 3.17)
project(CW5 C)

set(CMAKE_C_STANDARD 99)

find_package(MPI REQUIRED)

include_directories(${MPI_INCLUDE_PATH})

SET(CMAKE_C_COMPILER mpicc)

add_executable(CW5 prefix.c)