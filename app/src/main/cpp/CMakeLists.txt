cmake_minimum_required(VERSION 3.22.1)
project(proroot_host)
add_library(proroot_host SHARED pty.cpp)
target_compile_options(proroot_host PRIVATE -Wall -Wextra -Werror)
