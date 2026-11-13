#!/bin/sh
# From repository root: sh test_scheduler.sh
set -eu
cd "$(dirname "$0")"
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -g \
 -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude \
 scheduler_stress.c src/thread_pool.c src/queue.c src/task.c src/logger.c src/error.c \
 -lpthread -o "$BUILD/stress"
"$BUILD/stress"
printf '%s\n' 'Passed: 100,000 fast tasks, 50 pool lifecycles, exact completion metrics.'
