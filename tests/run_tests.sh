#!/bin/bash
#
# Simple test runner for mysh
# Run from the project root directory: bash tests/run_tests.sh
#

MYSH=./mysh
PASS=0
FAIL=0
TOTAL=0

red='\033[0;31m'
green='\033[0;32m'
reset='\033[0m'

run_test() {
    local description="$1"
    local input="$2"
    local expected="$3"

    TOTAL=$((TOTAL + 1))
    actual=$(echo "$input" | $MYSH 2>/dev/null)

    if [ "$actual" = "$expected" ]; then
        echo -e "${green}PASS${reset}: $description"
        PASS=$((PASS + 1))
    else
        echo -e "${red}FAIL${reset}: $description"
        echo "  expected: $(echo "$expected" | head -3)"
        echo "  got:      $(echo "$actual" | head -3)"
        FAIL=$((FAIL + 1))
    fi
}

run_batch_file() {
    local description="$1"
    local file="$2"
    local expected="$3"

    TOTAL=$((TOTAL + 1))
    actual=$($MYSH "$file" 2>/dev/null)

    if [ "$actual" = "$expected" ]; then
        echo -e "${green}PASS${reset}: $description"
        PASS=$((PASS + 1))
    else
        echo -e "${red}FAIL${reset}: $description"
        echo "  expected: $(echo "$expected" | head -3)"
        echo "  got:      $(echo "$actual" | head -3)"
        FAIL=$((FAIL + 1))
    fi
}

# make sure mysh is built
if [ ! -f "$MYSH" ]; then
    echo "Building mysh..."
    make
fi

if [ ! -f "$MYSH" ]; then
    echo "Failed to build mysh"
    exit 1
fi

echo "===== Running mysh tests ====="
echo ""

# --- basic echo ---
run_test "echo hello" "echo hello" "hello"
run_test "echo multiple words" "echo one two three" "one two three"
run_test "empty line" "" ""

# --- pwd ---
run_test "pwd prints cwd" "pwd" "$(pwd)"

# --- cd and pwd ---
REAL_TMP=$(cd /tmp && pwd -P)
run_test "cd to /tmp" "cd /tmp
pwd" "$REAL_TMP"

run_test "cd with no arg goes home" "cd
pwd" "$HOME"

# --- which ---
run_test "which ls" "which ls" "$(which ls 2>/dev/null)"
run_test "which cd fails (builtin)" "which cd" ""

# --- comments ---
run_test "comment line is ignored" "# nothing here" ""
run_test "inline comment" "echo visible # ignored" "visible"

# --- exit ---
run_test "exit stops execution" "echo before
exit
echo after" "before"

# --- redirection ---
run_test "output redirect" "echo hello > /tmp/mysh_test_redir.txt
cat /tmp/mysh_test_redir.txt" "hello"

run_test "input redirect" "echo contents > /tmp/mysh_test_in.txt
cat < /tmp/mysh_test_in.txt" "contents"

# --- pipes ---
run_test "simple pipe" "echo hello | cat" "hello"
run_test "double pipe" "echo hello | cat | cat" "hello"

# --- absolute path ---
run_test "absolute path command" "/bin/echo absolute" "absolute"

# --- error recovery ---
run_test "continues after bad cd" "cd /no_such_dir_abc
echo still here" "still here"

run_test "continues after syntax error" "< <
echo recovered" "recovered"

# --- batch file mode ---
run_batch_file "batch file: basic" "tests/test_basic.sh" "hello world
$(pwd)
done"

run_batch_file "batch file: exit" "tests/test_exit.sh" "before exit"

# --- wildcards (run from project dir) ---
run_test "wildcard *.c expands" "echo *.c" "$(echo *.c)"

echo ""
echo "===== Results: $PASS/$TOTAL passed, $FAIL failed ====="

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
