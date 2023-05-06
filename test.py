import socket
import threading
import time

# set up the echo server address and port
SERVER_ADDRESS = ('128.2.100.189', 8888)

# set up the number of requests to send
NUM_REQUESTS = 100000

DATA_LEN = 1024 * 1024

# define a function to send a request to the server
def send_request():
    # create a socket object
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    # connect to the echo server
    client_socket.connect(SERVER_ADDRESS)

    # send the message to the server
    DATA = "1" * DATA_LEN
    client_socket.send(DATA.encode())
    client_socket.send(str('\n').encode())

    response = b''
    expected_bytes = DATA_LEN

    # receive the response from the server
    while len(response) < expected_bytes:
        chunk = client_socket.recv(expected_bytes - len(response))
        if not chunk:
            # Handle case where server closed connection prematurely
            break
        response += chunk

    # Do something with the response
    # print(response.decode())


    # print(len(response))


    # close the socket
    client_socket.close()

    return response

# define a function to send multiple requests to the server
def send_requests(num_requests):
    # send the requests and measure the response time
    start_time = time.time()
    for i in range(num_requests):
        response = send_request()
    end_time = time.time()

    # calculate the average response time
    avg_response_time = (end_time - start_time) / num_requests

    # print the result
    print(f'Sent {num_requests} requests in {end_time - start_time} seconds')
    print(f'Average response time: {avg_response_time} seconds')

# send multiple requests concurrently using multi-threading
threads = []
for i in range(NUM_REQUESTS):
    thread = threading.Thread(target=send_request)
    threads.append(thread)

start_time = time.time()
for thread in threads:
    thread.start()

for thread in threads:
    thread.join()
end_time = time.time()

# calculate the total time taken and the requests per second
total_time = end_time - start_time
rps = NUM_REQUESTS / total_time

# print the result
print(f'Sent {NUM_REQUESTS} requests in {total_time} seconds')
print(f'Requests per second: {rps}')
