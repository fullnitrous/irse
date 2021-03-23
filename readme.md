# NOTE THIS IS STILL UNDER DEVELOPMENT

# IRSE - Image Reverse Searching Engine


Irse is a reverse image searching application written in C/C++ which loads a file containing ids and hashes. It reads the file and creates a vantage point tree in memory which allows for quick quieries on the data.

Communication to the program is done via a socket connection.

# Socket protocol

Irse operates on pure sockets, therefore it is important to communicate approperiately in order to request the right information and to be able to receive the right information.

The following text spesifies the protocol in order to talk to the irse server.

## Rebuild Tree Command

### Description

This command reloads the hash file and rebuilds the tree. Beaware that this stalls all other queries for the duration of the rebuild time.

### Spesification

In order to rebuild the tree, the following is sent to the server.

```
2 bytes
----------------------------
(uint16_t), value = 60 001 |
```

One the server is finished rebuilding the tree the following response is sent.

```
2 bytes
-----------------------
(uint16_t), value = 1 |
```

## Query Command

### Description

This command spesifies how to query to the server. The query command takes in a spesific hash to try to find and how many results to display.

### Spesification

The following is the query packet. Where n_results is how many results to return and hash is the haystack value to search for.

```
10 bytes
------------------------------------------
(uint16_t), n_results | (uint64_t), hash |
```

The server then replies with.

```
16 bytes
----------------------------------------------------------------------
(uint32_t) resp_size | (uint64_t) query_hash | (uint32_t) query_time | <- header

+

13 bytes * n_results
---------------------------------------------------------
(uint32_t) id | (uint64_t) result_hash | (uint8_t) dist | <- body
```

* `resp_size` is the size of the entire response (header + body).

* `query_hash` is the hash originally requested to search on. This is returned in order to check that the hash was correctly received. 

* `query_time` which is the time it took to query. Important to note that it is the time to query, not the time from once the query request was received until a answer was generated.

* `id` is the id of the picture returned.

* `result_hash` is the hash of the picture returned.

* `dist` is the hamming distance between `result_hash` and `query_hash`.