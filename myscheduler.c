#include <stdio.h>
#include <stdlib.h>
//  you may need other standard header files
#include <string.h>
#include <stdbool.h>

//  CITS2002 Project 1 2023
//  Student1:   23890702   THOMAS DUCK
//  Student2:   23456379   CALLUM LAMB


//  myscheduler (v1.0)
//  Compile with:  cc -std=c11 -Wall -Werror -o myscheduler myscheduler.c


//  THESE CONSTANTS DEFINE THE MAXIMUM SIZE OF sysconfig AND command DETAILS
//  THAT YOUR PROGRAM NEEDS TO SUPPORT.  YOU'LL REQUIRE THESE //  CONSTANTS
//  WHEN DEFINING THE MAXIMUM SIZES OF ANY REQUIRED DATA STRUCTURES.

#define MAX_DEVICES                     4
#define MAX_DEVICE_NAME                 20
#define MAX_COMMANDS                    10
#define MAX_COMMAND_NAME                20
#define MAX_SYSCALLS_PER_PROCESS        40
#define MAX_RUNNING_PROCESSES           50

//  NOTE THAT DEVICE DATA-TRANSFER-RATES ARE MEASURED IN BYTES/SECOND,
//  THAT ALL TIMES ARE MEASURED IN MICROSECONDS (usecs),
//  AND THAT THE TOTAL-PROCESS-COMPLETION-TIME WILL NOT EXCEED 2000 SECONDS
//  (SO YOU CAN SAFELY USE 'STANDARD' 32-BIT ints TO STORE TIMES).

#define DEFAULT_TIME_QUANTUM            100

#define TIME_CONTEXT_SWITCH             5
#define TIME_CORE_STATE_TRANSITIONS     10
#define TIME_ACQUIRE_BUS                20


//  ----------------------------------------------------------------------

#define CHAR_COMMENT                    '#'

// Define constants for the max length of the system call name and for the usecs string
// With the only system calls being read, write, spawn, sleep, wait, and exit, along with a need to store the usecs string, length cannot be more than 5
#define MAX_SYSCALL_LENGTH              6

// Define a max line length at 100 characters
#define MAX_LINE_LENGTH                 100

#define EMPTY_INDEX                     -1

// Define a structure that will store each device
// Each device has a name, read and write speed, and a queue that will store any command using
// that device to read or write
struct device {
    char name[MAX_DEVICE_NAME];
    int read_speed;
    int write_speed;
    int device_queue[MAX_RUNNING_PROCESSES + 1];
};

// The array of devices
struct device devices[MAX_DEVICES];

// Set the time quantum equal to the default, but with the ability to be changed depending on the sysconfig file
int time_quantum = DEFAULT_TIME_QUANTUM;

// Initialise CPU time and total time to 0, will be added to as required
int CPU_time = 0;
int total_time = 0;

// This variable will allow usecs to be stored separately when reading commands into their structures
char usecs[MAX_SYSCALL_LENGTH];

// Define a structure that will store each system call
// Each system call has a time that it will execute at, a name of the process to be executed,
// a process or device depending on what is in the name, and a size or time depeding on if
// it is I/O or sleep
struct system_call {
    int time;
    char name[MAX_SYSCALL_LENGTH];
    char process_device[MAX_COMMAND_NAME];
    int size_time;
};

// Define a structure for each command
// Each command will store the name of the command along with its array of system calls
struct command {
    char name[MAX_COMMAND_NAME];
    struct system_call system_calls[MAX_SYSCALLS_PER_PROCESS];
};

// Array of command structures
struct command commands[MAX_COMMANDS];

// Define a structure to hold command states
// This will allow for access of commands outside the array of command structures using integers
struct command_state {
    // Index of the command in the command structures array we are running 
    int command_index;

    // Index of the system call of the command in the array of command structures we are running
    int system_call_index;

    // Elapsed execution time for the command
    int execution_time;

    // Amount of time we will block for when added to a blocking queue
    int block_time;

    // Total time at which point the block will expire, set once it is the head of a blocking queue
    int block_expiration_time;

    // Array of any commands the current command spawns so that it does not exit early when being held in wait
    int spawned_command_states[MAX_RUNNING_PROCESSES];
};

// Array of command states
// These are the states of actual command executions
// Accessing this array is how we will run through the system calls of each command that is called
// Size is 50 + 1 so that the final index will always be empty
struct command_state command_states[MAX_RUNNING_PROCESSES + 1];

// This variable will allow us to check which device is accessing the data-bus when doing I/O
int device_accessing_bus = EMPTY_INDEX;

// Initialise four queues to hold commands based on where they are supposed to be
// The values stored in these arrays will be the command indexes which allow us to execute the commands
// Size is 50 + 1 so that the final index will always be empty
int ready_queue[MAX_RUNNING_PROCESSES + 1]; 
int sleep_queue[MAX_RUNNING_PROCESSES + 1];
int exit_queue[MAX_RUNNING_PROCESSES + 1];
int wait_queue[MAX_RUNNING_PROCESSES + 1];

// A function to add a command index to a queue
// Find the first empty slot and add the new index
void add_to_queue(int queue[], int cmd_state_index) {
    int i = 0;
    while(queue[i] >= 0) {i++;}
    queue[i] = cmd_state_index;
}

// A function to add a command index to a queue when there is a state transition involved
// Send the command index to the add_to_queue function, and add the time for the state transition
void add_to_queue_with_transition(int queue[], int cmd_state_index) {
    add_to_queue(queue, cmd_state_index);
    total_time += TIME_CORE_STATE_TRANSITIONS;
}

// A function to remove a command index from a queue
// This is called when moving a command index from one queue to another
// It will find the index that is to be removed and copy everything below that up one space, including a -1 where the final index used to be
void remove_from_queue(int queue[], int index) {
    for(int i = index; queue[i] >= 0; i++) {
        queue[i] = queue[i + 1];
    }
}

// A function to initialise each process
// The command index is the command to be executed
// System call index is 0 to start from the first system call of the relevant command
// Initialise the spawned processes array to be empty
void initialise_process(int command_index, int process_id) {
    // Initialise the command_state
    command_states[process_id].command_index = command_index;

    // Start from first system call
    command_states[process_id].system_call_index = 0;
    for (int i = 0; i < MAX_RUNNING_PROCESSES; i++) {
        command_states[process_id].spawned_command_states[i] = EMPTY_INDEX;
    }
}

// A function to create a process
// This is used for the very first command which executes automatically, and for any spawned processes
// This takes in a command index and finds the first available index where it can be added in the array of command states
int create_process(int command_index) {
    int i = 0;
    while(command_states[i].command_index >= 0) {
        i++;
    }

    // Initialise the command_state
    initialise_process(command_index, i);

    // Add the new command state to the ready queue
    return i;
}

// Read in the sysconfig file
// This will read in the devices
void read_sysconfig(char argv0[], char filename[]) {

    // Open the file
    FILE *sysconfigF = fopen(filename, "r");

    // Set a max line length
    char line[MAX_LINE_LENGTH];

    // Store Bps so that it does not get added to a structure
    char bps[MAX_SYSCALL_LENGTH];

    // Counter for the number of devices
    int device_count = 0;

    // Iterate through each line of the sysconfig file
    while (fgets(line, sizeof(line), sysconfigF)!= NULL) {

        // Ignore any lines that start with a #
        if (line[0]=='#'){
            continue;
        }

        //Store device so that it does not get added to the structure
        char device[MAX_DEVICE_NAME];

        // Device name
        char name[MAX_DEVICE_NAME];

        // Device read and write speeds
        int read_speed;
        int write_speed;

        // Count the number of items read
        int items_read = sscanf(line, "%s %s %d %s %d", device, name, &read_speed, bps, &write_speed);

        // If there are five items read, add the name along with the read and write speeds to the structure
        // Increment the device counter
        if (items_read == 5 && device_count< MAX_DEVICES) {
            strcpy(devices[device_count].name, name);
            devices[device_count].read_speed = read_speed;
            devices[device_count].write_speed = write_speed;
            device_count++;
        }

        // If there are two items read, set the time quantum
        else if (items_read == 2 && strcmp(device, "timequantum")==0) {
            time_quantum = atoi(name);
        }
    }

    // Close the sysconfig file
    fclose(sysconfigF);

    // Sort the devices from fastest read speed to slowest read speed
    // This will help when sorting through I/O commands
    for (int i = 0; i < MAX_DEVICES; i++) {
        for (int j = i; j < MAX_DEVICES; j++) {
            if (devices[i].read_speed < devices[j].read_speed) {
                struct device placeholder;
                placeholder = devices[i];
                devices[i] = devices[j];
                devices[j] = placeholder;
            }
        }
    }
}

//  ----------------------------------------------------------------------

// Read the command file
// This will read in the commands and system calls
void read_commands(char argv0[], char filename[]) {

    // Open the file
    FILE *commandsF = fopen(filename, "r");

    // Set a max line length
    char line[MAX_LINE_LENGTH];

    // Counter variable for commands
    int i = 0;

    // Iterate through each line of the command file
    while (fgets(line, sizeof(line), commandsF)!= NULL) {

        // Ignore any lines that start with a #
        if (line[0]=='#') {
            continue;
        }

        // Scan the command name
        sscanf(line, "%s", commands[i].name);

        // Counter variable for system calls
        int j = 0;

        // Iterate through each system call of the command
        while (fgets(line, sizeof(line), commandsF) != NULL) {

            // Break this loop to start a new command if there is a line starting with a #
            if (line[0] == '#') {
                break;
            }

            // Read the execution time and the name of the system call into the system call structure within the command structure
            sscanf(line, "%d%s%s", &commands[i].system_calls[j].time, usecs, commands[i].system_calls[j].name);

            // If the name is read or write, additionally read the device the I/O will operate on, as well as the size of the I/O
            if (strcmp(commands[i].system_calls[j].name, "read") == 0 || strcmp(commands[i].system_calls[j].name, "write") == 0) {
                sscanf(
                    line,
                    "%d %s  %s  %s  %d",
                    &commands[i].system_calls[j].time,
                    usecs,
                    commands[i].system_calls[j].name,
                    commands[i].system_calls[j].process_device,
                    &commands[i].system_calls[j].size_time
                );
            }

            // If the name is sleep, additionally read the time to be sleeping for
            else if (strcmp(commands[i].system_calls[j].name, "sleep") == 0) {
                sscanf(
                    line, 
                    "%d %s  %s  %d", 
                    &commands[i].system_calls[j].time,
                    usecs,
                    commands[i].system_calls[j].name,
                    &commands[i].system_calls[j].size_time
                );
            }

            // If the name is spawn, additionally read the process that will be spawned
            else if (strcmp(commands[i].system_calls[j].name, "spawn") == 0) {
                sscanf(
                    line, 
                    "%d %s  %s  %s", 
                    &commands[i].system_calls[j].time,
                    usecs,
                    commands[i].system_calls[j].name,
                    commands[i].system_calls[j].process_device
                );
            }
            // Nothing additional needs to be read if the name is wait or exit, as nothing comes after those system calls

            // Increment j to read the next system call
            j++;
        }

        // Increment i to read the next command
        i++;
    }

    // Close the command file
    fclose(commandsF);
}

//  ----------------------------------------------------------------------

// A function to initialise the index of every queue to the empty index of -1
void initialise_queues() {
    
    // Initialise the state queues
    for (int i = 0; i < MAX_RUNNING_PROCESSES + 1; i++) {
        ready_queue[i] = EMPTY_INDEX;
        sleep_queue[i] = EMPTY_INDEX;
        exit_queue[i] = EMPTY_INDEX;
        wait_queue[i] = EMPTY_INDEX;

        // Use a nested for loop to initialise each device queue, as this is effectively a 2D array
        for (int j = 0; j < MAX_DEVICES; j++) {
            devices[j].device_queue[i] = EMPTY_INDEX;
        }

        // Also initialise the command states
        command_states[i].command_index = EMPTY_INDEX;
    }
}

//  ----------------------------------------------------------------------

// A function to check if there is a command still in a non-exit state
// If any queue except the exit queue has something in it, then there are still running commands
// Return true if something is still active, if nothing is active then false will be returned and the program ends
int has_running_command() {
    if (ready_queue[0] >= 0) {
        return true;
    }
    else if (sleep_queue[0] >= 0) {
        return true;
    }
    else if (wait_queue[0] >= 0) {
        return true;
    }
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (devices[i].device_queue[0] >= 0) {
            return true;
        }
    }

    return false;
}

//  ----------------------------------------------------------------------

// A function to check if a child process has exited the CPU
// This function checks the queue of spawned commands of the relevant parent command,
// and whether or not that particular child process is in the exit queue
// Returns true if the child in the exit queue, false otherwise
int has_exited(int process_id) {
    for (int j = 0; exit_queue[j] >= 0; j++) {
        int released_process = exit_queue[j];
        if (process_id == released_process) {
            return true;
        }
    }
    return false;
}

//  ----------------------------------------------------------------------

// A function to handle the sleep system call
// This function reads in the running state index, the system call index, and the time to sleep for
// The function then sets the time at which sleep will expire for the relevant command
// The running index of the command is then put into the sleep queue
void handle_sleep(int running_index, int syscall_index, int sleep_time) {
    sleep_time = commands[running_index].system_calls[syscall_index].size_time;
    command_states[running_index].block_expiration_time = sleep_time + total_time;
    add_to_queue_with_transition(sleep_queue, running_index);
}

//  ----------------------------------------------------------------------

// A function to handle the spawn system call
// This function reads in the running state index and the string of the command to be spawned
// This function uses a for loop to find the index in the array of commands that the spawned process is at
// The process is then created and added to the ready queue, as well as the parent command's array of child processes
// The loop then breaks and the parent process is added back into the ready queue
void handle_spawn(int running_index, char to_be_spawned[]) {

    for (int i = 0; i < MAX_COMMANDS; i++) {
        if (strcmp(to_be_spawned, commands[i].name) == 0) {
            int process = create_process(i);
            
            add_to_queue(command_states[running_index].spawned_command_states, process);
            add_to_queue(ready_queue, process);
            break;
        }
    }

    add_to_queue_with_transition(ready_queue, running_index);
}

//  ----------------------------------------------------------------------

// A function to handle the read and write system calls
// This function reads in the name read or write, the device to be read from or written to, the size of the I/O, and the running state index
// This function then searches for the relevant device using a for loop, and calculates the I/O time after determining if it is a read or write
// This function then sets the block time for the relevant command into that command's command state
// This function then sends the command to the relevant device queue via the running state index
void handle_read_write(int running_index, char name[], char device[], int size) {
    int device_IO_speed;

    for (int i = 0; i < MAX_DEVICES; i++) {
        char device_name[MAX_DEVICE_NAME];
        strcpy(device_name, devices[i].name);

        if (strcmp(device_name, device) == 0) {
            if (strcmp(name, "read") == 0) {
                device_IO_speed = devices[i].read_speed;
            }
            else if (strcmp(name, "write") == 0) {
                device_IO_speed = devices[i].write_speed;
            }


            // Using long ints here to not overflow memory when calculating the numerator of the I/O time
            long int IO_time_numerator = (long int)size*1000000;
            long int IO_time = IO_time_numerator/device_IO_speed + TIME_ACQUIRE_BUS;

            // If there is a remainder in the I/O time calculation then add an extra microsecond as a decimal will always round down
            if ((IO_time_numerator%device_IO_speed) > 0) {
                IO_time++;
            }

            command_states[running_index].block_time = IO_time;

            add_to_queue_with_transition(devices[i].device_queue, running_index);
        }
    }
}

//  ----------------------------------------------------------------------

// A function to handle the wait system call
// This function reads in the running state index
// This function then uses a for loop to find if there are child processes to wait for by checking if they have exited the CPU
// If there are is a child process active the function returns as it cannot exit wait
// If there are no child processes remaining, the command is sent to the ready queue via the running state index
void handle_wait(int running_index) {
    for (int i = 0; command_states[running_index].spawned_command_states[i] >= 0; i++) {
        if (has_exited(command_states[running_index].spawned_command_states[i])) {
            continue;
        }
        else {
            add_to_queue_with_transition(wait_queue, running_index);
            return;
        }
    }
    add_to_queue_with_transition(ready_queue, running_index);
}

//  ----------------------------------------------------------------------

// A function to handle each system call
// This function reads in the running state index and the system call index
// This function then initialises the command index to the running state index and initialises name to the name of the system call being run
// This function then increments the total time by one microsecond to account for the time spent deciding where to send each command after the running state
// This function then decides what to do with the command based on the name of the system call, and sends it to the appropriate function where it is dealt with
void handle_system_call(int running_state_index, int system_call_index) {
    
    int command_index = command_states[running_state_index].command_index;
    char name[MAX_SYSCALL_LENGTH];
    strcpy(name, commands[command_index].system_calls[system_call_index].name);

    total_time++;

    // If the system call is sleep, send the command to the handle sleep function via the running state, along with the system call index and the sleep time
    if (strcmp(name, "sleep") == 0) {
        int sleep_time = commands[command_index].system_calls[system_call_index].size_time;
        handle_sleep(running_state_index, system_call_index, sleep_time);
    }

    // If the system call is spawn, send the command to the handle spawn function via the running state index, along with the process to be spawned
    else if (strcmp(name, "spawn") == 0) {
        char to_be_spawned[MAX_DEVICE_NAME];
        strcpy(to_be_spawned, commands[command_index].system_calls[system_call_index].process_device);

        handle_spawn(running_state_index, to_be_spawned);
    }

    // If the system call is read or write, first find the device upon which the I/O will be performed
    // Then send the command to the handle read/write function via the running state index, along with the name of the system call,
    // the previously found device, and the size of the I/O being performed
    else if (strcmp(name, "read") == 0 || strcmp(name, "write") == 0) {
        char command_device_name[MAX_DEVICE_NAME];
        strcpy(command_device_name, commands[command_index].system_calls[system_call_index].process_device);

        int IO_size = commands[command_index].system_calls[system_call_index].size_time;

        handle_read_write(running_state_index, name, command_device_name, IO_size);
    }

    // If the system call is wait, send the command to the handle wait function via the running state index
    else if (strcmp(name, "wait") == 0) {
        handle_wait(running_state_index);
    }

    // If the system call is exit, do not send the command to another function and instead immediately send it to the exit queue
    // via the running state index
    else if (strcmp(name, "exit") == 0) {
        // Add this to the exit queue. It is done.
        add_to_queue(exit_queue, running_state_index);
    }
}

//  ----------------------------------------------------------------------

// A function to run the next system call
// This function acts as the running state
// This function decides what to do with the running command based on whether or not the timequantum has been expired
void run_next_system_call(int running_state_index) {

    // Initialise variables for the command index, system call index, execution time, and remaining execution time
    int command_index = command_states[running_state_index].command_index;
    int system_call_index = command_states[running_state_index].system_call_index;
    int expected_execution_time = commands[command_index].system_calls[system_call_index].time;
    int remaining_execution_time = expected_execution_time - command_states[running_state_index].execution_time;

    // If the time quantum is not expired before the system call stops executing
    if (remaining_execution_time < time_quantum) {
        
        //Add the remaining execution time to the total time and the CPU time
        total_time += remaining_execution_time;
        CPU_time += remaining_execution_time;

        // Add the remaining execution time to the total execution time of the relevant command
        command_states[running_state_index].execution_time += remaining_execution_time;

        // Send the command to be handled for its next system call
        handle_system_call(running_state_index, system_call_index);

        // Increment the system call index
        command_states[running_state_index].system_call_index++;
    }

    // If the time quantum expires before the command stops executing
    else {
        // Add the time quantum to the total time and CPU time
        total_time += time_quantum;
        CPU_time += time_quantum;

        // Add the time quantum to the total execution time of the relevant command
        command_states[running_state_index].execution_time += time_quantum;

        // Send the command back to the ready queue
        add_to_queue_with_transition(ready_queue, running_state_index);
    }
}

//  ----------------------------------------------------------------------

// A function to execute the next command
// This function takes the command at the head of the ready queue and sends it to the run next system call function, if there is a command in the ready queue
// The command is then removed from the ready queue, and five microseconds are added to the total time for the context switch
void execute_next_command() {
    // Get the next command from head
    int running_index = ready_queue[0];
    if (running_index >= 0) {
        // Context switch is when we move from ready to running
        run_next_system_call(running_index);
        remove_from_queue(ready_queue, 0);
        total_time += TIME_CONTEXT_SWITCH;
    }
}

//  ----------------------------------------------------------------------

// A function to handle anything in the wait queue
// This function uses a for loop to sort through the wait queue
// If something is found in the wait queue, check that command's queue of spawned processes to see if anything is there that has exited using the has exited function
// If all spawned processes are found to have exited, the command is sent back to the ready queue and removed from the wait queue
void handle_waiting_processes() {
    for (int i = 0; wait_queue[i] >= 0; i++) {
        int waiting_process = wait_queue[i];
        for (int j = 0; command_states[waiting_process].spawned_command_states[j] >= 0; j++) {
            int wait_for_process = command_states[waiting_process].spawned_command_states[j];

            if (has_exited(wait_for_process)) {
                remove_from_queue(command_states[waiting_process].spawned_command_states, j);

                // Because we removed an element
                j--;
            }
        }
        if (command_states[waiting_process].spawned_command_states[0] < 0) {
            add_to_queue_with_transition(ready_queue, waiting_process);
            remove_from_queue(wait_queue, i);

            // Because we removed an element
            i--;
        }
    }
}

//  ----------------------------------------------------------------------

// A function to handle processes that read or write
// This function checks to see if a command is waiting in any device queue, and executes accordingly
void handle_IO_processes() {

    // Check if there is a device to be accessed
    if (device_accessing_bus >= 0) {

        // Set the I/O process as the head of the queue of the device accessing the data-bus
        int IO_process = devices[device_accessing_bus].device_queue[0];

        // Set expiration time equal to the block expiration time of the I/O process, set later in this function
        int expiration_time = command_states[IO_process].block_expiration_time;

        // If the expiration time is at the current microsecond or has passed, send the command to the ready queue and remove it from its device queue
        // Set the index of the device accessing the data-bus to -1
        if (expiration_time <= total_time) {
            add_to_queue_with_transition(ready_queue, IO_process);
            remove_from_queue(devices[device_accessing_bus].device_queue, 0);
            device_accessing_bus = EMPTY_INDEX;
        }

        // If the expiration time of the command currently on the bus has not passed, return the function immediately
        else {
            return;
        }
    }

    // If the above if statement is not passed, check the device queues to see if anything is there, starting at the queue of the
    // device with the fastest read speed
    for (int i = 0; i < MAX_DEVICES; i++) {

        // Set the I/O process equal to the head of the current queue
        int IO_process = devices[i].device_queue[0];

        // If there is an I/O process in the current queue
        if (IO_process >= 0) {

            // Set the current command's block expiration time to the block time calculated in the handle read/write function, and add the total time
            // Then set the device accessing the bus to the device of the current queue and return immediately
            command_states[IO_process].block_expiration_time = command_states[IO_process].block_time + total_time;
            device_accessing_bus = i;
            return;
        }
    }
}

//  ----------------------------------------------------------------------

// A function to handle anything in the sleep queue
// This function uses a for loop to sort through the sleep que
// If the expiration time of the sleep is less than the current time, send the command to the ready queue and remove it from the sleep queue
void handle_sleeping_processes() {
    for (int i = 0; sleep_queue[i] >= 0; i++) {
        int sleeping_process = sleep_queue[i];
        if (command_states[sleeping_process].block_expiration_time < total_time) {
            add_to_queue_with_transition(ready_queue, sleeping_process);
            remove_from_queue(sleep_queue, i);

            // Because we removed an element
            i--;
        }
    }
}

//  ----------------------------------------------------------------------


// A function to execute commands
// Before any command is executed, this function initialise all queues to be empty using the initialise queues function
// This function then creates the first process that is executed automatically and sends it to the ready queue
// This function then uses a while loop to check if any commands are still in any queue except the exit queue
// This function then sees if a command can be executed, or removed from any device, sleep, or wait queues
void execute_commands(void) {

    initialise_queues();

    int process = create_process(0);
    add_to_queue(ready_queue, process);

    while (has_running_command()) {

        // Set the last total time equal to the current total time
        int last_time = total_time;

        execute_next_command();

        handle_IO_processes();

        handle_sleeping_processes();

        handle_waiting_processes();

        // Increment the total time by one microsecond if and only if nothing has executed or been removed from any device, sleep, or wait queues
        if (last_time == total_time) {
            total_time++;
        }
    }
}

//  ----------------------------------------------------------------------

int main(int argc, char *argv[]) {
    //  ENSURE THAT WE HAVE THE CORRECT NUMBER OF COMMAND-LINE ARGUMENTS
    if(argc != 3) {
        printf("Usage: %s sysconfig-file command-file\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    //  READ THE SYSTEM CONFIGURATION FILE
    read_sysconfig(argv[0], argv[1]);

    //  READ THE COMMAND FILE
    read_commands(argv[0], argv[2]);

    //  EXECUTE COMMANDS, STARTING AT FIRST IN command-file, UNTIL NONE REMAIN
    execute_commands();

    //  PRINT THE PROGRAM'S RESULTS
    int percentage = (CPU_time*100)/total_time;

    // Print the final results of the execution
    // printf("\nprompt> ./myscheduler  sysconfig-file  command-file\n");
    // printf("found 3 devices\n");
    // printf("time quantum is 120\n");
    // printf("found 6 commands\n");
    // printf("what a fun project!\n");
    // printf("measurements  482400  24\n\n");
    // printf("This took a long time and we wanted to have fun with the output\n");
    // printf("Our real output is below here\n\n\n");
    printf("Total CPU execution time:  %d\n", CPU_time);
    printf("measurements  %d %d\n\n", total_time, percentage);

    exit(EXIT_SUCCESS);
}

//  vim: ts=8 sw=4