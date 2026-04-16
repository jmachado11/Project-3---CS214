echo redirect test > /tmp/mysh_test_out.txt
cat /tmp/mysh_test_out.txt
echo second line > /tmp/mysh_test_out.txt
cat /tmp/mysh_test_out.txt
cat < /tmp/mysh_test_out.txt
echo both ways < /dev/null > /tmp/mysh_test_both.txt
cat /tmp/mysh_test_both.txt
