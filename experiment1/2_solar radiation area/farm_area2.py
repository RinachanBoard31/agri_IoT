from gpiozero import MCP3002
import time
import RPi.GPIO as GPIO
import spidev

import ambient
import requests

import datetime

### set by user ###
THRESHOLD = 600.0  #kW
IRRIGATION_TIME = 180
###################

channel_id = 69723
write_key = 'YOUR_WRITE_KEY'
file_path = './val_total.txt'
relay_pin = 23  # GPIO23
LED_PIN_R = 16
LED_PIN_G = 21
LED_PIN_B = 20

# SPI Communication
spi = spidev.SpiDev()
spi.open(0, 0)
spi.max_speed_hz = 1000000


def setGPIO():
    GPIO.setmode(GPIO.BCM)
    GPIO.setup(relay_pin, GPIO.OUT)
    GPIO.setup(LED_PIN_R, GPIO.OUT)  # red
    GPIO.setup(LED_PIN_G, GPIO.OUT)  # green
    GPIO.setup(LED_PIN_B, GPIO.OUT)  # blue


def readPVSS03(ch):
    Vref = 3.3
    resp = spi.xfer2([0x68, 0x00])
    volume = ((resp[0] << 8) + resp[1]) & 0x3FF
    
    val = round(volume*Vref/1024, 4)
    return val


def sendAmbient(doc):
    try: 
        am = ambient.Ambient(channel_id, write_key)
        res = am.send({'d1': doc['val_ave'], 'd2': doc['val_total'], 'd3': doc['is_irrigation']})
        print('send to Ambient (ret = %d)' % res.status_code)
        # sent to Ambient correctly, GREEN
        GPIO.output(LED_PIN_R, 0)
        GPIO.output(LED_PIN_G, 1)
        GPIO.output(LED_PIN_B, 0)
    except requests.exceptions.RequestException as e:
        print('request failed: ', e)
        # sent to Ambient incorrectly, RED
        GPIO.output(LED_PIN_R, 1)
        GPIO.output(LED_PIN_G, 0)
        GPIO.output(LED_PIN_B, 0)


def irrigation():
    # irrigation start, BLUE
    GPIO.output(LED_PIN_R, 0)
    GPIO.output(LED_PIN_G, 0)
    GPIO.output(LED_PIN_B, 1)

    GPIO.output(relay_pin, 1)
    print("irrigation start")
    time.sleep(IRRIGATION_TIME)
    
    GPIO.output(relay_pin, 0)
    print("irrigation stop")
    
    # irrigation stop, GREEN
    GPIO.output(LED_PIN_R, 0)
    GPIO.output(LED_PIN_G, 1)
    GPIO.output(LED_PIN_B, 0)
    
    #GPIO.cleanup(relay_pin) # free


def main():
    setGPIO()
    
    val_total = 0 
    f = open(file_path, 'r')
    val_total = float(f.read())  # 積算日射量
    f.close()
    
    while True:
        doc = {}
        count = 0
        val_sum = 0
        
        # 1min measurement
        while (count < 60):
            val = readPVSS03(ch=0)
            val_sum += val
            val_total += val
            count += 1
            
            val_sum = round(val_sum, 4)
            val_total = round(val_total, 4)
            
            print("[" + str(count) + "] val: " + str(val) + " sum: " + str(val_sum) + " total: " + str(val_total))
            time.sleep(1)
        
        val_ave = round(val_sum / count, 4)
        print("ave: " + str(val_ave))
        doc["val_ave"] = val_ave
        doc["val_total"] = val_total
        
        # send data to Ambient & irrigation
        dt_now = datetime.datetime.now()
        dt_today = datetime.date.today()
        dt_pre = datetime.datetime(dt_today.year, dt_today.month, dt_today.day, 7, 0)  # no irrigation before this
        dt_pro = datetime.datetime(dt_today.year, dt_today.month, dt_today.day, 15, 0)  # no irrigation after this
        
        if (val_total >= THRESHOLD and dt_now >= dt_pre and dt_now <= dt_pro):
            doc["is_irrigation"] = 1
            sendAmbient(doc)
            irrigation()
            val_total = 0  # reset total to 0
        else:
            doc["is_irrigation"] = 0
            sendAmbient(doc)
            
        # update total as txt
        f = open(file_path, 'w')
        f.write(str(val_total))
        f.close()


if __name__ == "__main__":
    main()
