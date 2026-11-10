CC      = gcc
CFLAGS  = -std=c17 -Wall -Wextra -Wpedantic -g -pthread
TARGET  = fetchall
SRC     = src/fetchall.c src/provided.c
HDR     = src/provided.h

.PHONY: all clean check-env check-1 check-2 check-3 check-4 test \
        run race experiment compare watch

all: $(TARGET)

$(TARGET): $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC)

# The same program with every mutex lock/unlock turned into a no-op.
# Used only by `make race`.
$(TARGET)-nolocks: $(SRC) $(HDR) tests/helpers/nolocks.h
	@$(CC) $(CFLAGS) -include tests/helpers/nolocks.h -o $@ $(SRC)

# ---------------------------------------------------------------------------
# check-env - run this FIRST, on day one.
# ---------------------------------------------------------------------------
check-env:
	@echo "CSC 139 environment check"
	@echo "-------------------------"
	@printf 'kernel      : '; uname -sr
	@printf 'cpu cores   : '; nproc
	@printf 'gcc         : '; if command -v gcc >/dev/null 2>&1; then gcc --version | head -1; else echo '(not installed)'; fi
	@printf 'make        : '; if command -v make >/dev/null 2>&1; then make --version | head -1; else echo '(not installed)'; fi
	@printf 'gdb         : '; if command -v gdb >/dev/null 2>&1; then gdb --version | head -1; else echo '(not installed)'; fi
	@echo "-------------------------"
	@if [ "$$(uname -s)" != "Linux" ]; then \
		echo 'platform    : FAIL - this is not Linux. Open your codespace.'; \
		exit 1; \
	fi
	@printf '#include <pthread.h>\n#include <semaphore.h>\nstatic void *f(void *a){return a;}\nint main(void){pthread_t t;sem_t s;if(sem_init(&s,0,1))return 1;sem_wait(&s);sem_post(&s);if(pthread_create(&t,0,f,0))return 1;return pthread_join(t,0);}\n' > /tmp/csc139_env.c
	@if gcc -pthread -o /tmp/csc139_env /tmp/csc139_env.c 2>/dev/null && /tmp/csc139_env; then \
		echo 'pthreads    : PASS - threads and semaphores work here'; \
	else \
		echo 'pthreads    : FAIL - could not build or run a threaded program'; \
		echo '              Post on Canvas with a screenshot of this output.'; \
		exit 1; \
	fi
	@rm -f /tmp/csc139_env /tmp/csc139_env.c
	@if man 3 sem_init >/dev/null 2>&1; then \
		echo 'man sem_init: PASS'; \
	else \
		echo 'man sem_init: (missing - not fatal, use man7.org instead)'; \
	fi
	@echo "-------------------------"
	@echo "Environment looks good. Read docs/BEYOND_THE_SLIDES.md, then start stage 1 in src/fetchall.c"

# ---------------------------------------------------------------------------
# Checkpoints: one per stage. Each prints a stage code when it passes.
# ---------------------------------------------------------------------------
check-1: $(TARGET)
	@bash tests/run_tests.sh 1
check-2: $(TARGET)
	@bash tests/run_tests.sh 2
check-3: $(TARGET)
	@bash tests/run_tests.sh 3
check-4: $(TARGET)
	@bash tests/run_tests.sh 4
test: $(TARGET)
	@bash tests/run_tests.sh all

# A small mixed list, four threads.
run: $(TARGET)
	@echo './$(TARGET) -t 4 examples/demo.txt'
	@./$(TARGET) -t 4 examples/demo.txt; echo "(fetchall exited with status $$?)"

# ---------------------------------------------------------------------------
# race - 50,000 instant fetches with 8 threads, five times with your locks
# and five times with every lock removed. The right answer is printed first.
# ---------------------------------------------------------------------------
race: $(TARGET) $(TARGET)-nolocks
	@mkdir -p tests/tmp
	@awk 'BEGIN { for (i = 1; i <= 50000; i++) printf "http://csc139.test/f/%05d.bin?bytes=%d\n", i, i * 1000 }' > tests/tmp/race.txt
	@echo "50,000 URLs, 8 threads. The correct summary is:"
	@echo "    50000 urls, 50000 ok, 0 failed, 1250025000000 bytes"
	@echo
	@echo "Your program, as written:"
	@for r in 1 2 3 4 5; do ./$(TARGET) -t 8 -q tests/tmp/race.txt | sed 's/^\[fetchall\] summary: /    /'; done
	@echo
	@echo "Your program with every pthread_mutex_lock/unlock removed:"
	@for r in 1 2 3 4 5; do ./$(TARGET)-nolocks -t 8 -q tests/tmp/race.txt | sed 's/^\[fetchall\] summary: /    /'; done

# ---------------------------------------------------------------------------
# experiment - examples/urls40.txt: 40 downloads of 0.25 s each.
# Part 1 varies the number of threads. Part 2 gives every URL its own thread
# (40 threads) and varies the connection limit. Takes about 30 seconds.
# ---------------------------------------------------------------------------
experiment: $(TARGET)
	@echo "40 downloads of 0.25 s each, on a machine with $$(nproc) CPU cores"
	@echo
	@echo "Part 1: more threads (-t), no connection limit"
	@printf '  %-8s %-10s\n' "-t" SECONDS
	@for t in 1 2 4 8 10 20 40; do \
		s=$$(./$(TARGET) -t $$t -q examples/urls40.txt | sed -nE 's/.* in ([0-9.]+)s$$/\1/p'); \
		printf '  %-8s %-10s\n' "$$t" "$${s:-?}"; \
	done
	@echo
	@echo "Part 2: 40 threads (one per URL), connection limit (-c)"
	@printf '  %-8s %-10s\n' "-c" SECONDS
	@for c in 2 4 5 10; do \
		s=$$(./$(TARGET) -t 40 -c $$c -q examples/urls40.txt | sed -nE 's/.* in ([0-9.]+)s$$/\1/p'); \
		printf '  %-8s %-10s\n' "$$c" "$${s:-?}"; \
	done

# ---------------------------------------------------------------------------
# compare - fixed slices versus a shared queue, on uneven work.
# ---------------------------------------------------------------------------
compare: $(TARGET)
	@mkdir -p tests/tmp
	@echo "examples/uneven.txt: 4 slow downloads (1.5 s) first, then 12 quick ones (0.25 s). 4 threads."
	@echo
	@./$(TARGET) -t 4 -m static examples/uneven.txt > tests/tmp/static.out
	@printf '  %-26s ' "-m static (fixed slices)"; sed -nE 's/.* in ([0-9.]+)s$$/\1 seconds/p' tests/tmp/static.out
	@printf '  %-26s ' "-m queue  (shared queue)"; ./$(TARGET) -t 4 -q -m queue examples/uneven.txt | sed -nE 's/.* in ([0-9.]+)s$$/\1 seconds/p'
	@echo
	@echo "With fixed slices, these workers fetched the four slow files:"
	@grep 'video' tests/tmp/static.out | sed 's/^/  /'

# ---------------------------------------------------------------------------
# watch - look at the threads while they wait for the "network".
# ---------------------------------------------------------------------------
watch: $(TARGET)
	@./$(TARGET) -t 8 -q examples/urls40.txt > /dev/null & pid=$$!; \
	sleep 0.5; \
	echo "ps -L -p $$pid  (one row per thread; S = asleep, R = running)"; \
	ps -L -o pid,lwp,stat,pcpu,wchan:16,comm -p $$pid; \
	wait $$pid

clean:
	rm -f $(TARGET) $(TARGET)-nolocks *.o
	rm -rf tests/tmp
