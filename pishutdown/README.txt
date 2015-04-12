 #
 # auto shutdown your pi on power fails to avoid damages
 #
 # script idea based on this tutorial:
 # http://www.instructables.com/id/Raspberry-Pi-Shutdown-Button/step4/Raspberry-Pi-Shutdown-script/
 #
 
 # place this folder like this:
 /home/pi/pishutdown
 
 # otherwise change pishutdown.sh & cron entry
 
 
# for autostart run:
sudo crontab -e

# and add this line to crontab :
@reboot sh /home/pi/pishutdown/pishutdown.sh >/home/pi/pishutdown/logs/cronlog 2>&1

