# Chuid 1.0 Design Documentation
 
Chuid is a tool for fast, parallel change of UID's/GID's according to a provided list of 2-tuples.
 
1. Input Specification
   * a set of filesystems
   * a list of 2-tuples for old uid's and gid's and corresponding new values
   * the number of threads to be created
   * the busy-percentage threshold given as a number between 0 and 1
 
2. Result Specification
 
For each regular file, directory or link UID and GID are checked against the list of 2-tuples
provided by the uidlist file. If there is a match with one of the entries the UID/GID will
be changed with the corresponding new UID/GID.
 
3. Algorithm
 
Central assumption underlying the algorithm is that the set of all reporting tree tops together
with their result information fits into main memory. (For our analysis runs, this is no real
restriction: With a reporting level of 2 and about 150,000 reporting directories all in all, we
need about 1GB of main memory for the whole analysis.)
 
Our multi-threaded algorithm is based on the following two principles:
- As file systems are by and large tree structures, scans of different subtrees are
independent from each other: Synchronization is necessary only in case of files
referenced by hardlinks.
- Threads need to dispatch work only when too many of them are idle.
 
Therefore, threads should be synchronized only in these two cases.
In particular, results in a reporting directory will be accumulated per thread; thus,
synchronization for result recording is not needed during the scan.
 
 
## Design
 
### Scan Phase:
 
-    Constant, configurable number of threads: no dynamic thread creation or deletion
during the scan phase.
 
-    Configurable threshold for the ratio of busy threads vs. total number of threads: If the
actual percentage of busy threads is lower than this threshold, the algorithm assumes
that too many threads are idle.
 
-    Threads synchronize at a global, mutex-ed stack holding subtree roots to be processed.
This global stack is initialized with the roots of all file systems given as input to the
algorithm.
Actually, there are two global stacks to differentiate between fast and slow file sources;
we describe their design below, at the end of the scan phase. Till then, the
simplification of one global stack makes it easier to understand the algorithm.
 
-    Each thread takes one node from the global stack and processes its subtree; if the stack
is empty, idle threads do a conditional wait for a nodes-available event.
 
-    Each thread has its own private stack for traversing the subtree of the node it took
from the global stack.
 
-    Each node ist checked whether UID and/or GID must be changed. If there is a match
UID/GID will be changed according to provided list of 2-tuples. This is always a disjunct
process in two steps to keep the freedom of changing UID/GID independently of each other.
 
If the child is a regular file with nlink greater than 1, the thread synchronizes at a
global hash table to check whether the file has been seen before: If not, the file is
registered in the hash, the mutex is released.
 
If the child is a directory, it is pushed on the private stack of the thread for further
processing.
 
Otherwise, only the Others-count field in the thread-specific structure is incremented.
 
-    After each child determination, the thread checks whether there are too many threads
idle (no synchronization necessary):
If so, it stops processing of the current node and, in case of remaining children to be
determined, stores the position of the next child in the node and pushes the node back
onto its private stack. After that, the thread checks whether there is more than one
node in its private stack and in that case prepends all nodes except for the first one to
the global stack. The thread continues with the remaining node in its private stack.
Otherwise (i.e., if there are enough threads busy), the thread just continues node
processing.
We considered applying a configurable check period here: A thread would test for too
many idle threads only after the amount of time given by the check period had passed
since the last test. We found, however, that testing after each child determination did
not impact performance (as no synchronization is needed); therefore, we set it
conceptually to 0 and removed it from the design.
 
-    The scan phase is completed when a thread finds the global stack empty and the
(mutex-ed) count of busy threads to be 0. In this case, it wakes all other threads by
signalling that work is finished.
 
-    Two global stacks
We generalize the one-global-stack concept to be able to choose between slow and fast
sources: There are two global stacks---the fast stack and the slow stack, each has a
speed associated with it 0; the fast stack is initialized with all file-system roots. The
idea is that if a thread hands over its private-stack elements,
   1.  It calculates its processing speed: The number of directory nodes it has
processed since it took the last node from the global stacks divided by the time
passed since then.
   2.  It determines whether they go to the fast or to the slow stack: If the processing
speed from Step (1) is above or equal to the average of the two speeds of the
stacks, the elements are prepended to the fast stack otherwise, they are
prepended to the slow stack.
   3.  The speed of the chosen stack is updated: new_speed = processing_speed
 
Thus, each stack's speed is kept updated by transfer events; these two stack speeds are
used for calculating how many elements are to be taken from the fast stack before the
next element is removed from the slow stack:
 
   1. This ratio is represented by a global counter, initialized to 0.
   2. If a thread wants to take a new subtree root from the global stacks, it checks the
counter: If the counter has a value different from 0, it decrements it and
removes an element from the fast stack. If the fast stack was empty, it
recalculates the counter and takes an element from the slow stack (see next
step).
   3. If  the counter is 0 and the slow stack is not empty, the thread sets the counter
to the ceiling of the ratio between the current speed of the fast stack and the
current speed of the slow stack; then, it removes an element from the slow
stack. If the slow stack was empty, it takes an element from the fast stack; the
counter remains 0.
 
 
Hans Argenton & Fritz Kink, May 2022
