#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <algorithm>
#include <iostream>
#include <vector>
#include <stdio.h>
#include <queue>
#include <limits>
#include <sys/time.h>
#include <iomanip>
#include <fstream>

#include "vptree.hpp"

#define CONN_TIMEOUT_SEC 1
#define CONN_TIMEOUT_USEC 0

std::string hashes_file;

void QueryPerformanceCounter( uint64_t* val )
{
	timeval tv;
	struct timezone tz = {0, 0};
	gettimeofday( &tv, &tz );
	*val = tv.tv_sec * 1000000 + tv.tv_usec;
}

struct Point {
	uint32_t id;
	uint64_t phash;
};

int hamming_dist(uint64_t bits1, uint64_t bits2) {
	long b = bits1 ^ bits2;
	b -= (b >> 1) & 0x5555555555555555L;
	b = (b & 0x3333333333333333L) + ((b >> 2) & 0x3333333333333333L);
	b = (b + (b >> 4)) & 0x0f0f0f0f0f0f0f0fL;
	return (b * 0x0101010101010101L) >> 56;
}

double distance( const Point& p1, const Point& p2 )
{
	uint64_t x = p1.phash;
	uint64_t y = p2.phash;	
	return hamming_dist(x, y);
}

struct thread_data
{
	int socket;
	struct sockaddr_in* addr;
	char* headers;
	pthread_rwlock_t* rwlock;
};

void encode(void* dest, void* in, size_t size, int* offset) {
	for(size_t i = 0; i < size; i++) {
		*(uint8_t*)((uint8_t*)dest + *offset + i) = *(uint8_t*)((uint8_t*)in + i);
	}
	*offset += size;
	return;
}

void decode(void* dest, void* buffer, size_t size, int* offset) {
	for(size_t i = 0; i < size; i++) {
		*(uint8_t*)((uint8_t*)dest + i) = *(uint8_t*)((uint8_t*)buffer + *offset + i);
	}
	*offset += size;
	return;
}

VPTree<Point, distance> tree;

void build_tree(void) {
	std::vector<Point> points;
	
	std::ifstream file(hashes_file, std::ios::binary | std::ios::ate);
	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);
	
	if(size % 12 == 0) {	
		std::vector<char> buffer(size);

		if(file.read(buffer.data(), size)) {
			std::cout << "file size: " << size << std::endl;
		} else {
			std::cout << "failed to read file" << std::endl;
			return;
		}
		
		uint8_t* cbuffer = (uint8_t*)&buffer[0];

		int bytes = 0;
		
		Point p;

		for(std::streamsize i = 0; i < size; i+=12) {
			decode((void*)&p.id, cbuffer, sizeof(uint32_t), &bytes);
			decode((void*)&p.phash, cbuffer, sizeof(uint64_t), &bytes);	
			points.push_back(p);
		}

	} else {
		std::cout << "error corrupted file" << std::endl;
	}

	file.close();

	uint64_t start, end;
	QueryPerformanceCounter( &start );
	tree.create( points );
	QueryPerformanceCounter( &end );
	printf("Tree creation took %d microseconds\n", (int)(end-start));

	return;
}

void* thread_function(void* data)
{
	struct thread_data* this_ = (struct thread_data*)data;
	uint8_t buffer[1024];
	int read_stat = 1;	
	while(read_stat != 0) {	
		read_stat = read(this_->socket , buffer, 1024);
		if(read_stat > 0) {
			if(read_stat == 10) {
				uint16_t n_results = 0;
				
				int bytes = 0;
				uint64_t hash = 0;

				decode((void*)&n_results, buffer, sizeof(uint16_t), &bytes);
				decode((void*)&hash, buffer, sizeof(uint64_t), &bytes);

				printf("%d, %lu\n", n_results, hash);

				uint32_t resp_size = 16 + n_results * 13;
				uint32_t query_time;
				
				uint8_t resp_buffer[1024];
					
				Point point;

				point.phash = hash;
				std::vector<Point> results;
				std::vector<double> distances;
				
				uint64_t start, end;
				uint64_t start2, end2;

				QueryPerformanceCounter(&start);	
				pthread_rwlock_rdlock(this_->rwlock);
				QueryPerformanceCounter(&start2);
				tree.search(point, n_results, &results, &distances);
				QueryPerformanceCounter(&end2);
				pthread_rwlock_unlock(this_->rwlock);
				QueryPerformanceCounter(&end);

				printf("t(tree.search) = %duS lock_overhead = %duS\n", (int)(end2-start2), ((int)(end-start)-(int)(end2-start2)));

				query_time = (uint32_t)(end-start);
				
				bytes = 0;

				encode(resp_buffer, (void*)&resp_size,  sizeof(uint32_t), &bytes);
				encode(resp_buffer, (void*)&hash,       sizeof(uint64_t), &bytes);
				encode(resp_buffer, (void*)&query_time, sizeof(uint32_t), &bytes);
				
				for(size_t i = 0; i < results.size(); i++ ) {
					//printf("%s %lg\n", results[i].image_name.c_str(), distances[i]);
					uint32_t id = i;
					uint64_t rhash = results[i].phash;
					uint8_t dist = distances[i];

					encode(resp_buffer, (void*)&id,    sizeof(uint32_t), &bytes);
					encode(resp_buffer, (void*)&rhash, sizeof(uint64_t), &bytes);
					encode(resp_buffer, (void*)&dist,  sizeof(uint8_t), &bytes);
				}
	
				send(this_->socket, resp_buffer, resp_size, 0);
			} else if(read_stat == 2) {
				uint16_t command = 0;
				int bytes = 0;
				decode((void*)&command, buffer, sizeof(uint16_t), &bytes);
				if(command == 60001) {
					pthread_rwlock_wrlock(this_->rwlock);
					build_tree();
					pthread_rwlock_unlock(this_->rwlock);
					send(this_->socket, "\x01", 2, 0);
				}

			} else {
				printf("malformed packet\n");
			}
			printf("\n");
		}
	}
	close(this_->socket);
	free(this_);
	return NULL;
}



void server_loop(int port)
{
	struct sockaddr_in server;
	int root_socket = socket(AF_INET, SOCK_STREAM, 0);
	if(root_socket != -1)
	{
		server.sin_family = AF_INET;
		server.sin_addr.s_addr = INADDR_ANY;
		server.sin_port = htons(port);
		int enable = 1;
		setsockopt(root_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
		
		if(bind(root_socket, (struct sockaddr*)&server, sizeof(server)) == 0)
		{
			listen(root_socket, __INT_MAX__);
			
			pthread_mutex_t thread_count_mutex;
			pthread_mutex_init(&thread_count_mutex, NULL);

			struct sockaddr_in temp_client_addr;
			int client_addr_len = sizeof(struct sockaddr_in);

			int incoming_socket;

			pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;

			pthread_t thread;
			pthread_attr_t attr;
			pthread_attr_init(&attr);
			pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
			
			printf("ip: %s\nport: %d\n", 
			inet_ntoa(server.sin_addr), ntohs(server.sin_port));

			struct timeval conn_timeout;      
			conn_timeout.tv_sec = CONN_TIMEOUT_SEC;
			conn_timeout.tv_usec = CONN_TIMEOUT_USEC;

			while(1)
			{
				if(0 < (incoming_socket = accept(root_socket, (struct sockaddr*)&temp_client_addr, (socklen_t*)&client_addr_len)))
				{
					setsockopt(incoming_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&conn_timeout, sizeof(conn_timeout));
						
					struct thread_data* thread_data = (struct thread_data*)malloc(sizeof(struct thread_data));
					thread_data->socket = incoming_socket;
					thread_data->addr = &temp_client_addr;
					thread_data->rwlock = &rwlock;
					pthread_create(&(thread), &attr, &thread_function, thread_data);

					printf("Connection established to %s on port %d.\n", 
					inet_ntoa(temp_client_addr.sin_addr), ntohs(temp_client_addr.sin_port));
				}
				else
				{
					printf("Connection could not be established to %s on port %d.\n", 
					inet_ntoa(temp_client_addr.sin_addr), ntohs(temp_client_addr.sin_port));
				}
			}
			close(root_socket);
		}
		else
		{
			printf("Server could not bind to %s on port %d.\n", 
			inet_ntoa(server.sin_addr), ntohs(server.sin_port));
		}
	}
	else
	{
		printf("Server could not create root socket.\n");
	}
	return;
}

int main(int argc, char* argv[]) {
	if(argc == 2) {
		hashes_file = argv[1];
		std::cout << hashes_file << std::endl;
	} else {
		std::cout << "need hash file" << std::endl;
		return 0;
	}

	build_tree();
	server_loop(6969);
	return 0;
}
