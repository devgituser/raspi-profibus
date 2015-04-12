#!/usr/bin/python

# Import the modules to send commands to the system and access GPIO pins
import RPi.GPIO as gpio
import os
from datetime import datetime

pin = 7	# 	pin number of RPI header
tol = 20	# ms 	time tolerance to detect alert
cyc = 100 # ms	toggle cycle by power fail monitor

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


#Set pin numbering to board numbering
gpio.setmode(gpio.BOARD)
gpio.setup(pin, gpio.IN) 

boot = datetime.now()

print_time(boot, "booted")
print("Script running: %s"%str(os.path.basename(__file__)) )

edge = [boot] # store any edge time here


while(1):
	# watch for pin toggle
	gpio.wait_for_edge(pin, gpio.FALLING) 
	edge.append(datetime.now())
	check()
	gpio.wait_for_edge(pin, gpio.RISING) 
	edge.append(datetime.now())
	check()