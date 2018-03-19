@echo off
SET RUN_PATH="%~dp0msys\bin\"
CD %RUN_PATH%
SET CMD_PATH="%~dp0"

echo Press Enter to snoop data! Otherwise close the program!
pause

sh --login -i < %CMD_PATH%get.txt


echo done
pause
exit

:_eof
pause
exit