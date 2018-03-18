@echo off
SET RUN_PATH="%~dp0msys\bin\"
CD %RUN_PATH%
SET CMD_PATH="%~dp0"

echo checking data! Otherwise close the program!
rem pause

:_check
if exist X:\data.txt (
sh --login -i < %CMD_PATH%send.txt
echo found
) else (
GOTO :_check
)

echo done
pause
exit

:_eof
pause
exit