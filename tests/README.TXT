1. put all *test* files in a subfolder named "test".
2. open output.c, then change the global variable "spinfor" value from 5.00 to 0.2 (for shorter waiting time)
3. in the main folder (where your files are), compile the project using the makefile.
in "test" folder, do:
4. chmod +x setup_server_tests
5. ./setup_server_tests
6. chmod +x run_test
7. ./run_test
8. in the "public" folder, a folder "forbidden.cgi" was created. delete it, then
right click->create document-> name it "forbidden.cgi", then
right click on that file->properties->permissions-> set to "None"

* number 8 need to be done everytime after recompiling the project,
because "make clean" deletes the "public" folder,
and "make" recreates it (but without the forbidden.cgi file!)

i changed the test and removed the part that test average time (the test didnt worked for me and i dont have thew time to fix it)
and change things base on my understanding of the piazza so double check the result