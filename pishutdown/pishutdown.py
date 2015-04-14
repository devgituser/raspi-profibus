#!/usr/bin/python

# Import the modules to send commands to the system and access GPIO pins
import RPi.GPIO as gpio
import os
from datetime import datetime
import time
import threading

alert = 7	# 	pin number of RPI header = GPIO4
alive = 22	# 	pin number of RPI header = GPIO25
tol = 20	# ms 	time tolerance to detect alert
cyc = 100 # ms	toggle cycle by power fail monitor
tal = 100	# ms toggle time for alive pin

def print_time(t,txt=""):	
	print("%d-%02d-%02d__%02d:%02d:%02d %s"%(\
		t.year,t.month,t.day,t.hour,t.minute,t.second,str(txt)))

def delta_of(newer,older):
	delta = newer - older
	return  delta.seconds*1000 + delta.microseconds/1000
	
def check():
	if len(edge) < 4:	# enough samples to comape?
		return
	min = cyc - 5
	max = cyc + tol
	if( 	(min < delta_of(edge[-1], edge[-2] ) < max) and \
		(min < delta_of(edge[-2], edge[-3] ) < max) and \
		(min < delta_of(edge[-3], edge[-4] ) < max) ):
		diffs = []
		for i in range(len(edge)-1):
			diffs.append( delta_of(edge[i+1], edge[i]) )	
		print_time(edge[-3],"diffs: "+str(diffs))
		# Shutdown
		os.system('shutdown now -h')
		exit()

class KeepAlive(threading.Thread):
	def __init__(self, pin_number, timeout):
		threading.Thread.__init__(self) # needed for thread stuff
		self.pin_nr = int(pin_number)
		self.to = float(timeout)
	
	def prepare(self):
		#gpio.setmode(gpio.BOARD)
		gpio.setup(self.pin_nr, gpio.OUT) 
	
	def run(self):
		i = 20 # for debugging, simulate non responsive rpi
		while(1):
			gpio.output(self.pin_nr, gpio.LOW)
			time.sleep( self.to )
			gpio.output(self.pin_nr, gpio.HIGH)
			time.sleep( self.to )
			i -= 1
		print_time(atetime.now(), "rpi stopped alive toggle")

#Set pin numbering to board numbering
gpio.setmode(gpio.BOARD)
gpio.setup(alert, gpio.IN) 


boot = datetime.now()

print_time(boot, "booted")
#print("Script running: %s"%str(os.path.basename(__file__)) )

edge = [boot] # store any edge time here

ka = KeepAlive(alive,float(tal)/1000)
ka.prepare()
ka.setDaemon(True)	# auto kills thread at end of script
ka.start()

while(1):
	# watch for pin toggle
	gpio.wait_for_edge(alert, gpio.FALLING) 
	edge.append(datetime.now())
	check()
	gpio.wait_for_edge(alert, gpio.RISING) 
	edge.append(datetime.now())
	check()
