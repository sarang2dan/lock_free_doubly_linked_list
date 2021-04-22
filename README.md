# Lock-free Doubly Linked List


## How to Test

### Build test
```
$ make test
$ ./bin/test
```

### Usage

```
$ ./bin/test
    Usage: test <item #> <insert threads #> <read threads #>
        <item #>: count of item that would be inserted and read
```

### Run test

#### Environment vars
- count of item that are inserted & read : 5,000,000
- insert threads: 10
- read threads: 15

#### Execute & Result

print infomation of data table, 10 times

- Meaning of each field
  - total aging #: Total count of aged nodes.
  - data list #: remained data list node count
  - aging list #: remaind aging list node count

```
$ time ./bin/test 5000000 10 15
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

