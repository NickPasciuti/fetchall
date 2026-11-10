#!/usr/bin/env bash
# CSC 139 - Assignment 3 checkpoints.
#
#   bash tests/run_tests.sh 1      stage 1 only   (make check-1)
#   bash tests/run_tests.sh 2      stage 2 only   (make check-2)
#   bash tests/run_tests.sh 3      stage 3 only   (make check-3)
#   bash tests/run_tests.sh 4      stage 4 only   (make check-4)
#   bash tests/run_tests.sh all    every stage    (make test)
#
# When every check in a stage passes, that stage's code is printed. You need
# the four stage codes for the Canvas quiz.
#
# Every run of your program is wrapped in a 20-second timeout, so a version
# that deadlocks fails a check instead of freezing your codespace.

set -u

BIN=./fetchall
TMP=tests/tmp
CODE_1="CSC139-A3-S1-FD172C77"
CODE_2="CSC139-A3-S2-BFB042CD"
CODE_3="CSC139-A3-S3-B9E33FEF"
CODE_4="CSC139-A3-S4-294C3050"

if [ "$(uname -s)" != "Linux" ]; then
  echo "These checks must be run on Linux. Open your codespace."
  exit 1
fi
if [ ! -x "$BIN" ]; then
  echo "fetchall is not built. Run: make"
  exit 1
fi

what="${1:-all}"
rm -rf "$TMP"; mkdir -p "$TMP"

ok()  { printf '  \033[32mPASS\033[0m  %s\n' "$1"; stage_pass=$((stage_pass+1)); }
bad() { printf '  \033[31mFAIL\033[0m  %s\n' "$1"; stage_fail=$((stage_fail+1)); }
why() { printf '        %s\n' "$@"; }

# urls FILE COUNT MS [EXTRA] : write COUNT urls that each take MS milliseconds.
# Sizes are 1000, 2000, 3000 ... bytes, so a lost or doubled fetch changes the
# byte total.
urls() {
  awk -v n="$2" -v ms="$3" 'BEGIN { for (i = 1; i <= n; i++)
        printf "http://csc139.test/f/%05d.bin?ms=%d&bytes=%d\n", i, ms, i * 1000 }' > "$1"
}
# expected byte total for urls COUNT
total_bytes() { echo $(( $1 * ($1 + 1) / 2 * 1000 )); }

# run_fa ARGS... : stdout -> $TMP/out, stderr -> $TMP/err, stats -> $TMP/stats
run_fa() {
  rm -f "$TMP/stats"
  FETCHALL_STATS="$TMP/stats" timeout 20 "$BIN" "$@" > "$TMP/out" 2> "$TMP/err"
  rc=$?
  timed_out=0; crashed=0
  [ $rc -eq 124 ] && timed_out=1
  [ $rc -ge 128 ] && [ $rc -ne 124 ] && crashed=1
}

# stat NAME : one number from the bookkeeping file (-1 if it is missing)
stat() {
  local v
  v=$(grep -E "^$1=" "$TMP/stats" 2>/dev/null | cut -d= -f2)
  echo "${v:--1}"
}

# summary values
sum_line() { grep -E '^\[fetchall\] summary: ' "$TMP/out" | tail -1; }
has_summary() { # URLS OK FAILED BYTES
  sum_line | grep -Eq "^\[fetchall\] summary: $1 urls, $2 ok, $3 failed, $4 bytes in [0-9]+\.[0-9]{2}s$"
}
elapsed() { sum_line | sed -nE 's/.* in ([0-9]+\.[0-9]{2})s$/\1/p'; }
# between X LO HI : is LO <= X <= HI ?
between() { [ -n "$1" ] && awk -v x="$1" -v lo="$2" -v hi="$3" 'BEGIN { exit !(x >= lo && x <= hi) }'; }

show_out() {
  if [ "$timed_out" -eq 1 ]; then why "(still running after 20 seconds and was stopped: a deadlock, or a thread nobody joined?)"; fi
  if [ "$crashed" -eq 1 ]; then why "(the program crashed: exit status $rc)"; fi
  if [ -s "$TMP/out" ]; then
    why "your program printed (last lines):"
    tail -6 "$TMP/out" | sed 's/^/          | /'
  else
    why "your program printed nothing on stdout."
  fi
  if [ -s "$TMP/err" ]; then
    why "and on stderr:"
    head -6 "$TMP/err" | sed 's/^/          | /'
  fi
}

show_stats() {
  why "fetches made: $(stat calls)   URLs never fetched: $(stat never_fetched)   URLs fetched twice: $(stat fetched_twice)" \
      "threads that fetched: $(stat threads)   fetches made by main(): $(stat main_fetches)   most in flight at once: $(stat peak)"
}

# uses SYMBOL... : does the compiled program call every one of these?
uses() {
  local syms s
  syms=$(nm -u "$BIN" 2>/dev/null)
  for s in "$@"; do
    echo "$syms" | grep -Eq "(^|[[:space:]])$s(@|$)" || { missing="$s"; return 1; }
  done
  return 0
}

all_once() { [ "$(stat never_fetched)" = "0" ] && [ "$(stat fetched_twice)" = "0" ] && [ "$(stat unknown)" = "0" ]; }

# ---------------------------------------------------------------------------
stage1() {
  echo "Stage 1 - threads"

  urls "$TMP/six.txt" 6 50
  echo 'http://csc139.test/gone.html?ms=50&status=404' >> "$TMP/six.txt"
  run_fa "$TMP/six.txt"
  if has_summary 7 6 1 "$(total_bytes 6)" && [ $rc -eq 1 ] && all_once \
     && [ "$(stat threads)" = "1" ] && [ "$(stat main_fetches)" = "0" ]; then
    ok "with one thread, fetches every URL once and the totals are right"
  else
    bad "with one thread, fetches every URL once and the totals are right"
    why "expected: summary 7 urls, 6 ok, 1 failed, $(total_bytes 6) bytes, fetched by 1 worker thread"
    show_stats; show_out
  fi

  urls "$TMP/twelve.txt" 12 300
  run_fa -t 4 "$TMP/twelve.txt"
  if has_summary 12 12 0 "$(total_bytes 12)" && all_once \
     && [ "$(stat threads)" = "4" ] && [ "$(stat main_fetches)" = "0" ]; then
    ok "-t 4: four worker threads fetch the URLs, each URL exactly once"
  else
    bad "-t 4: four worker threads fetch the URLs, each URL exactly once"
    show_stats; show_out
  fi

  t=$(elapsed)
  if [ "$(stat peak)" = "4" ] && between "$t" 0.85 1.40; then
    ok "-t 4: the threads really run at the same time (12 x 0.3 s in about 0.9 s)"
  else
    bad "-t 4: the threads really run at the same time (12 x 0.3 s in about 0.9 s)"
    why "took ${t:-?} s; most fetches in flight at once: $(stat peak) (expected 4)." \
        "If it took about 3.6 s, you are joining each thread right after creating it."
  fi

  urls "$TMP/ten.txt" 10 30
  run_fa -t 3 "$TMP/ten.txt"
  got=$(grep -E '^\[fetchall\] worker [0-9]+: ' "$TMP/out" \
        | sed -nE 's/^\[fetchall\] worker ([0-9]+): .*\/f\/0*([0-9]+)\.bin.*/\2 \1/p' | sort -n | awk '{printf "%s ", $2}')
  want="1 1 1 2 2 2 3 3 3 3 "
  if [ "$got" = "$want" ]; then
    ok "-t 3 on 10 URLs: worker 1 gets URLs 1-3, worker 2 gets 4-6, worker 3 gets 7-10"
  else
    bad "-t 3 on 10 URLs: worker 1 gets URLs 1-3, worker 2 gets 4-6, worker 3 gets 7-10"
    why "worker that fetched URL 1..10, expected: $want" \
        "                              yours:    ${got:-(no fetch lines)}" \
        "Use first = k * nurls / T and end = (k + 1) * nurls / T, and id = k + 1."
  fi

  urls "$TMP/join.txt" 6 400
  run_fa -t 3 "$TMP/join.txt"
  last=$(tail -1 "$TMP/out")
  if [ "$(stat inflight_at_exit)" = "0" ] && [ "$(stat calls)" = "6" ] \
     && echo "$last" | grep -q '^\[fetchall\] summary: 6 urls, 6 ok'; then
    ok "joins every thread before printing the summary"
  else
    bad "joins every thread before printing the summary"
    why "the summary must be the last line, after every fetch has finished."
    show_out
  fi

  if uses pthread_create pthread_join; then
    ok "calls pthread_create() and pthread_join()"
  else
    bad "calls pthread_create() and pthread_join()"
    why "your compiled program never calls $missing()."
  fi
}

# ---------------------------------------------------------------------------
stage2() {
  echo "Stage 2 - a mutex"

  if uses pthread_mutex_lock pthread_mutex_unlock; then
    ok "calls pthread_mutex_lock() and pthread_mutex_unlock()"
  else
    bad "calls pthread_mutex_lock() and pthread_mutex_unlock()"
    why "your compiled program never calls $missing()."
  fi

  N=50000
  urls "$TMP/stress.txt" $N 0
  want_b=$(total_bytes $N)
  wrong=0; first_bad=""
  for r in 1 2 3 4 5 6 7 8 9 10; do
    run_fa -t 8 -q "$TMP/stress.txt"
    if ! has_summary $N $N 0 "$want_b"; then
      wrong=$((wrong+1)); [ -z "$first_bad" ] && first_bad="$(sum_line)"
    fi
  done
  if [ $wrong -eq 0 ]; then
    ok "totals are exact on 10 runs of 50,000 instant fetches with 8 threads"
  else
    bad "totals are exact on 10 runs of 50,000 instant fetches with 8 threads"
    why "$wrong of 10 runs got the totals wrong. Expected:" \
        "  [fetchall] summary: $N urls, $N ok, 0 failed, $want_b bytes" \
        "first wrong one:" "  ${first_bad:-(no summary line)}"
  fi

  urls "$TMP/twelve.txt" 12 300
  run_fa -t 4 "$TMP/twelve.txt"
  t=$(elapsed)
  if has_summary 12 12 0 "$(total_bytes 12)" && between "$t" 0.85 1.40; then
    ok "with the lock in place, the fetches still overlap (12 x 0.3 s, -t 4, about 0.9 s)"
  else
    bad "with the lock in place, the fetches still overlap (12 x 0.3 s, -t 4, about 0.9 s)"
    why "took ${t:-?} s. Hold the lock only while updating the totals, never while fetching."
    show_out
  fi
}

# ---------------------------------------------------------------------------
stage3() {
  echo "Stage 3 - a counting semaphore"

  if uses sem_init sem_wait sem_post; then
    ok "calls sem_init(), sem_wait() and sem_post()"
  else
    bad "calls sem_init(), sem_wait() and sem_post()"
    why "your compiled program never calls $missing()."
  fi

  urls "$TMP/twelve.txt" 12 300
  run_fa -t 8 -c 3 "$TMP/twelve.txt"
  t=$(elapsed)
  if [ "$(stat peak)" = "3" ] && has_summary 12 12 0 "$(total_bytes 12)" && all_once \
     && between "$t" 1.15 1.70; then
    ok "-t 8 -c 3: exactly three fetches in flight at once, never more (about 1.2 s)"
  else
    bad "-t 8 -c 3: exactly three fetches in flight at once, never more (about 1.2 s)"
    why "most in flight at once: $(stat peak) (expected 3); took ${t:-?} s (expected about 1.2)"
    show_out
  fi

  urls "$TMP/one.txt" 6 100
  run_fa -t 4 -c 1 "$TMP/one.txt"
  t=$(elapsed)
  if [ "$(stat peak)" = "1" ] && has_summary 6 6 0 "$(total_bytes 6)" && between "$t" 0.58 0.95; then
    ok "-t 4 -c 1: one fetch at a time, even with four threads (about 0.6 s)"
  else
    bad "-t 4 -c 1: one fetch at a time, even with four threads (about 0.6 s)"
    why "most in flight at once: $(stat peak) (expected 1); took ${t:-?} s"
    show_out
  fi

  urls "$TMP/eight.txt" 8 200
  run_fa -t 8 "$TMP/eight.txt"
  if [ "$(stat peak)" = "8" ] && has_summary 8 8 0 "$(total_bytes 8)"; then
    ok "with no -c, there is no limit: -t 8 has eight fetches in flight"
  else
    bad "with no -c, there is no limit: -t 8 has eight fetches in flight"
    why "most in flight at once: $(stat peak) (expected 8). Initialise the semaphore to max_conn."
    show_out
  fi

  urls "$TMP/three_ok.txt" 3 50
  { echo 'http://csc139.test/gone.html?ms=50&status=404'
    echo 'http://csc139.test/also-gone.html?ms=50&status=500'
    cat "$TMP/three_ok.txt"; } > "$TMP/fail.txt"
  run_fa -t 2 -c 1 "$TMP/fail.txt"
  if has_summary 5 3 2 "$(total_bytes 3)" && [ "$timed_out" -eq 0 ]; then
    ok "a failed fetch gives its connection slot back (-c 1, two failures first)"
  else
    bad "a failed fetch gives its connection slot back (-c 1, two failures first)"
    why "call sem_post() whether the fetch worked or not."
    show_out
  fi
}

# ---------------------------------------------------------------------------
stage4() {
  echo "Stage 4 - a bounded buffer"

  urls "$TMP/twenty.txt" 20 100
  run_fa -t 4 -m queue "$TMP/twenty.txt"
  t=$(elapsed)
  if has_summary 20 20 0 "$(total_bytes 20)" && all_once && [ "$(stat threads)" = "4" ] \
     && [ "$(stat main_fetches)" = "0" ] && [ "$(stat inflight_at_exit)" = "0" ] \
     && between "$t" 0.48 0.90; then
    ok "-m queue -t 4: four workers share the queue, each URL once (about 0.5 s)"
  else
    bad "-m queue -t 4: four workers share the queue, each URL once (about 0.5 s)"
    why "took ${t:-?} s"; show_stats; show_out
  fi

  urls "$TMP/ten.txt" 10 50
  run_fa -t 1 -b 2 -m queue "$TMP/ten.txt"
  if [ "$(stat ring_high)" = "2" ] && has_summary 10 10 0 "$(total_bytes 10)" && [ "$crashed" -eq 0 ]; then
    ok "-b 2: the producer fills both slots, and never a third"
  else
    bad "-b 2: the producer fills both slots, and never a third"
    why "fullest the buffer got: $(stat ring_high) (expected exactly 2)." \
        "The empty-slots semaphore starts at the capacity; the full-slots one at 0."
    show_out
  fi

  urls "$TMP/b1.txt" 12 30
  run_fa -t 3 -b 1 -m queue "$TMP/b1.txt"
  if has_summary 12 12 0 "$(total_bytes 12)" && all_once && [ "$crashed" -eq 0 ] && [ "$timed_out" -eq 0 ]; then
    ok "-b 1 -t 3: works with a one-slot buffer"
  else
    bad "-b 1 -t 3: works with a one-slot buffer"
    show_stats; show_out
  fi

  urls "$TMP/three.txt" 3 100
  run_fa -t 8 -m queue "$TMP/three.txt"
  if has_summary 3 3 0 "$(total_bytes 3)" && [ "$timed_out" -eq 0 ] && [ "$(stat inflight_at_exit)" = "0" ]; then
    ok "-t 8 on 3 URLs: every worker gets a sentinel and stops, and main joins them all"
  else
    bad "-t 8 on 3 URLs: every worker gets a sentinel and stops, and main joins them all"
    why "put one SENTINEL into the queue per worker, after the last URL."
    show_out
  fi

  cp examples/uneven.txt "$TMP/uneven.txt"
  run_fa -t 4 -m queue "$TMP/uneven.txt"
  t=$(elapsed)
  if has_summary 16 16 0 '[0-9]+' && between "$t" 2.20 2.70; then
    ok "balances the load: examples/uneven.txt with -t 4 in about 2.25 s (static takes 6)"
  else
    bad "balances the load: examples/uneven.txt with -t 4 in about 2.25 s (static takes 6)"
    why "took ${t:-?} s"; show_out
  fi

  N=50000
  urls "$TMP/stress.txt" $N 0
  want_b=$(total_bytes $N)
  wrong=0; first_bad=""
  for r in 1 2 3 4 5; do
    run_fa -t 8 -b 4 -q -m queue "$TMP/stress.txt"
    if ! has_summary $N $N 0 "$want_b" || ! all_once || [ "$crashed" -eq 1 ] || [ "$timed_out" -eq 1 ]; then
      wrong=$((wrong+1))
      [ -z "$first_bad" ] && first_bad="$(sum_line) $(head -3 "$TMP/err" | tr '\n' ' ')"
    fi
  done
  if [ $wrong -eq 0 ]; then
    ok "no lost or doubled jobs: 5 runs of 50,000 instant fetches, 8 workers, 4 slots"
  else
    bad "no lost or doubled jobs: 5 runs of 50,000 instant fetches, 8 workers, 4 slots"
    why "$wrong of 5 runs went wrong. First one:" "  ${first_bad:-(no output)}" \
        "Every ring_push() and ring_pop() must happen while holding the buffer's mutex."
  fi
}

# ---------------------------------------------------------------------------
run_stage() {
  stage_pass=0; stage_fail=0
  "stage$1"
  local code_var="CODE_$1"
  if [ $stage_fail -eq 0 ]; then
    printf '  Stage %s passed (%d checks).  Stage code: \033[1m%s\033[0m\n\n' "$1" "$stage_pass" "${!code_var}"
    passed_stages="$passed_stages $1"
  else
    printf '  Stage %s: %d passed, %d failed.  Fix the failures and run it again.\n\n' "$1" "$stage_pass" "$stage_fail"
    failed_stages="$failed_stages $1"
  fi
}

passed_stages=""; failed_stages=""
echo "fetchall checkpoints"
echo "--------------------"
case "$what" in
  1|2|3|4) run_stage "$what" ;;
  all)     for s in 1 2 3 4; do run_stage $s; done ;;
  *)       echo "usage: $0 1|2|3|4|all"; exit 2 ;;
esac

if [ "$what" = "all" ]; then
  echo "--------------------"
  if [ -z "$failed_stages" ]; then
    echo "All four stages pass. Your stage codes, for Questions 1-4 of the quiz:"
  else
    echo "Stages still failing:$failed_stages"
    [ -n "$passed_stages" ] && echo "Stage codes you have earned so far:"
  fi
  for s in $passed_stages; do v="CODE_$s"; echo "    Stage $s: ${!v}"; done
fi

[ -z "$failed_stages" ]
