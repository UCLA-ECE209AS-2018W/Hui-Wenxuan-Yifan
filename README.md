# Repository for EE 209AS Winter 2018
# Relay Attack on Vehicle Keyless Entry System
Team: Hui Wang, Wenxuan Mao, Yifan Zhang

The Hardware Tool we used is Proxmark3, which is a completely open source tool \n
Their Github Webpage could be found at the link: https://github.com/Proxmark/proxmark3
Additional Hardware information could be found on the wiki page of the link above
##############################################################

The repository contains two batch file that we wrote to enable automatically detect and send RF signals in Windows 10 system

auto_get.bat and get.txt need to be in the same folder
auto_get script is for detecting the signal from the car 
#############################################################
auto_send.bat and send.txt need to be in the same folder
auto_send script is for replay the signal to the car key

The two scripts will not work with the Proxmark3 SDR
Proxmark3 clients are necessary for using the SDR
The client could be downloaded at the link: https://github.com/Gator96100/ProxSpace/archive/master.zip
The two scripts need to be placed in the ProxSpace-master folder
All the codes inside Proxmark3 repository (https://github.com/Proxmark/proxmark3) need to be downloaded and copied into the pm3 folder
