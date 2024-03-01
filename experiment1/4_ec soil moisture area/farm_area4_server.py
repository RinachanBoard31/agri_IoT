from http.server import HTTPServer, BaseHTTPRequestHandler
import os
import re
import logging
import socket

import gspread
from oauth2client.service_account import ServiceAccountCredentials

host_list = socket.gethostbyname_ex(socket.gethostname())
lan_address = host_list[2][0]

dirname = os.path.dirname(__file__)
server_ip = "YOUR_IP_ADDRESS"
server_port = 0000000000  # 指定ポート番号

lan_url = "http://{}:{:d}".format(lan_address, server_port)
url = "http://{}:{:d}".format(server_ip, server_port)


def load_file(path):
    with open(path, mode='r') as f:
        s = f.read()
        return s.encode('utf-8')


class S(BaseHTTPRequestHandler):
    def do_GET(self):
        m = re.match(r'^/threshold$', self.path)
        if m != None:
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.end_headers()
            self.wfile.write(load_file("./threshold.txt"))

    def do_POST(self):
        print('======================= POST =======================')
        print(self.path)

        m = re.match(r'^/data$', self.path)
        if m != None:
            content_length = int(self.headers['Content-Length']) # <--- Gets the size of data
            post_data = self.rfile.read(content_length) # <--- Gets the data itself
            print('======================= HTTP Request =======================')
            logging.info("POST request,\nPath: %s\nHeaders:\n%s\n\nBody:\n%s\n",
                str(self.path), str(self.headers), post_data.decode('utf-8'))

            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write('{"status":"ok"}'.encode('utf-8'))


def get_spread_sheet():
    scope = ['https://spreadsheets.google.com/feeds','https://www.googleapis.com/auth/drive']
    json = 'YOUR_DB_ID.json'
    credentials = ServiceAccountCredentials.from_json_keyfile_name(json, scope)
    gc = gspread.authorize(credentials)
    SPREADSHEET_KEY = 'YOUR_SPREADSHEET_KEY'
    worksheet = gc.open_by_key(SPREADSHEET_KEY).sheet1

    cellData = worksheet.acell('A2').value

    print("getSpreadSheet(): " + str(cellData))
    
    f = open('threshold.txt', 'w')
    f.write(cellData)
    f.close()


def web_server():
    logging.basicConfig(level=logging.INFO)
    
    httpd = HTTPServer(('', server_port), S)

    logging.info('Starting httpd...\n')
    httpd.serve_forever()


if __name__ == "__main__":
    print(lan_url)
    print(url)
    get_spread_sheet()
    web_server()