#!/bin/sh
SHTDN="home/pi/pishutdown"
cd /
cd $SHTDN
sudo python pishutdown.py >> /$SHTDN/logs/pi_logs
cd /
