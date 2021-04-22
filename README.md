# Lock-free Doubly Linked List
## ⛔️ WARNING
- Test on LINUX: it works.
- Test on MacOS: NOT works.
  - I just guess MacOS have some problem, not sure.
  - Maybe problems comes from compiler, atomic function or memory fence.
  - Under MacOS,  you will get better test results when you have did set smaller total thread counts than system's CPU core.
- No test on WINDOWS.

# How to Build

## Command line
``` 
$ make
or
$ make all
$ ls ./lib/*
```
You can open 'Makefile' to know other make options.

# How to Test

## Build test
```
$ make build_test
$ ./bin/lf_dlist_test
```

## Test Program Usage

```
$ ./bin/lf_dlist_test --help
 - Usage: lf_dlist_test [options]
   options:
	-t,	for test printing usage
	-h,  --help
		print help
	-i,  --num-thr-insert=<val>
		count of insert threads
	-r,  --num-thr-read=<val>
		count of read threads
	-n,  --item-count=<val>
		count of item that would be inserted and read

```

## Run test

### Describe vars
- count of item that are inserted & read : 5,000,000
- insert threads: 10
- read threads: 15

### Execute & Result

print infomation of data table, 10 times

- Meaning of each field
  - total aged #: Total count of aged nodes.
  - data list #: remained data list node count
  - aging list #: remaind aging list node count

```
ime bin/lf_dlist_test --item-count=5000000 --num-thr-insert=10 --num-thr-read=15 --verbose
[total aging #:500000][data list #:4216950][aging list #:308]
[total aging #:1000000][data list #:3971950][aging list #:28050]
[total aging #:1500000][data list #:3498796][aging list #:1204]
[total aging #:2000000][data list #:2999960][aging list #:40]
[total aging #:2500000][data list #:2491006][aging list #:8994]
[total aging #:3000000][data list #:1999773][aging list #:227]
[total aging #:3500000][data list #:1498664][aging list #:1336]
[total aging #:4000000][data list #:995319][aging list #:4681]
[total aging #:4500000][data list #:499484][aging list #:516]
[total aging #:5000000][data list #:0][aging list #:0]

real	0m16.723s
user	1m10.555s
sys	0m1.351s

$
```

