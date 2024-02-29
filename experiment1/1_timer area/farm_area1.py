from __future__ import print_function

import datetime
import os.path
import time

from google.auth.transport.requests import Request
from google.oauth2.credentials import Credentials
from google_auth_oauthlib.flow import InstalledAppFlow
from googleapiclient.discovery import build
from googleapiclient.errors import HttpError

import ambient
import requests

import RPi.GPIO as GPIO

SCOPES = ['https://www.googleapis.com/auth/calendar.readonly']

### ユーザ設定 ###
IRRIGATION_TIME = 1200  # 灌水時間[s]
################

relay_pin = 23  # GPIO23
LED_PIN_R = 16
LED_PIN_G = 21
LED_PIN_B = 20

irrigation_today = ''  # txtに記載されている日付
irrigation_num = 0     # txtに記載されている灌水回数
irrigation_list = []   # | 0 or 1 | time

channel_id = 00000000         # ambientのチャンネルID
write_key = 'YOUR_WRITE_KEY'  # ambientのライトキー


def setGPIO():
    GPIO.setmode(GPIO.BCM)
    GPIO.setup(relay_pin, GPIO.OUT)
    GPIO.setup(LED_PIN_R, GPIO.OUT)
    GPIO.setup(LED_PIN_G, GPIO.OUT)
    GPIO.setup(LED_PIN_B, GPIO.OUT)


def readIrrigationList():
    global irrigation_today
    global irrigation_num
    global irrigation_list

    f = open('./irrigation_list.txt', 'r', encoding='UTF-8')
    
    i = 0
    while True:
        data = f.readline().rstrip('\n')
        if data == '':
            break
        else:
            if i == 0:
                irrigation_today = datetime.datetime.fromisoformat(data)
            elif i == 1:
                irrigation_num = int(data)
            else:
                irrigation_list.append(data)
            i += 1
    f.close()


def writeIrrigationList(today, num, list):
    f = open('./irrigation_list.txt', 'w')
    f.write(str(today)+'\n')
    f.write(str(num)+'\n')
    for item in list:
        f.write(str(item)+'\n')
    f.close()


def getCalendar():
    creds = None

    if os.path.exists('./token.json'):
        creds = Credentials.from_authorized_user_file('./token.json', SCOPES)

    if not creds or not creds.valid:
        if creds and creds.expired and creds.refresh_token:
            creds.refresh(Request())
        else:
            flow = InstalledAppFlow.from_client_secrets_file(
                './credentials.json', SCOPES)
            creds = flow.run_local_server(port=0)
        with open('./token.json', 'w') as token:
            token.write(creds.to_json())

    try:
        service = build('calendar', 'v3', credentials=creds)

        # Call the Calendar API
        events_result = service.events().list(
            calendarId = 'YOUR_CALENDAR_ID@group.calendar.google.com', 
            timeMin = datetime.date.today().isoformat() + 'T00:00:00+09:00',
            timeMax = datetime.date.today().isoformat() + 'T23:59:59+09:00',
            maxResults = 20, 
            singleEvents = True,
            orderBy = 'startTime'
            ).execute()
        events = events_result.get('items', [])

        if not events:
            print('No events found.')
            return
        
        f = open('./irrigation_list.txt', 'w')
        f.write(str(datetime.date.today())+'\n')
        f.write(str(len(events))+'\n')

        for event in events:
            start = event['start'].get('dateTime', event['start'].get('date'))
            f.write('0 ' + start[11:19] + '\n')  

        f.close()

    except HttpError as error:
        print('An error occurred: %s' % error)
        # http error, RED
        GPIO.output(LED_PIN_R, 1)
        GPIO.output(LED_PIN_G, 0)
        GPIO.output(LED_PIN_B, 0)


def irrigation():
    global IRRIGATION_TIME
    
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


def sendAmbient(doc):
    try:
        am = ambient.Ambient(channel_id, write_key)
        res = am.send({'d1': doc['is_irrigation']})
        print('send to Ambient (ret = %d)' % res.status_code)
        # sent to Ambient correctly, GREEN
        GPIO.output(LED_PIN_R, 0)
        GPIO.output(LED_PIN_G, 1)
        GPIO.output(LED_PIN_B, 0)
    except requests.exceptions.RequestException as e:
        print('request failed : ', e)
        # unsent to Ambient, RED
        GPIO.output(LED_PIN_R, 1)
        GPIO.output(LED_PIN_G, 0)
        GPIO.output(LED_PIN_B, 0)


def main():
    global irrigation_today
    global irrigation_num
    global irrigation_list
    
    setGPIO()
    
    while True:
        doc = {}  ## for Ambient
        current_time = datetime.datetime.now()
        irrigation_list = []

        readIrrigationList()

        ## txtが今日のものでなかったら更新
        dt_today = datetime.date.today()
        if (datetime.datetime(dt_today.year, dt_today.month, dt_today.day, 0, 0) != irrigation_today or irrigation_today == ''): 
            irrigation_list = []
            getCalendar()
            readIrrigationList()

        ## 今灌水 or 灌水していないが過ぎてしまったものがないか確認、あったらすぐに灌水
        is_need = False
        copy_list = irrigation_list.copy()
        for i in range(len(irrigation_list)):
            irrigation_time = datetime.datetime.fromisoformat(str(dt_today)+' '+irrigation_list[i][2:10])
            if (irrigation_list[i][0] == '0' and irrigation_time <= current_time):
                is_need = True
                copy_list[i] = '1 ' + irrigation_list[i][2:10]
        # 灌水する場合
        if (is_need == True):
            irrigation_list = copy_list.copy()
            writeIrrigationList(dt_today, irrigation_num, irrigation_list)
            irrigation()
            doc['is_irrigation'] = 1
            sendAmbient(doc)
        else:
            doc['is_irrigation'] = 0
            sendAmbient(doc)
                
        time.sleep(50)


if __name__ == '__main__':
    main()
