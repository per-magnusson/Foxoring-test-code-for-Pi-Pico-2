/*******************************************************************
    Copyright (C) 2009 FreakLabs
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
    3. Neither the name of the the copyright holder nor the names of its contributors
       may be used to endorse or promote products derived from this software
       without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
    OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
    HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
    OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
    SUCH DAMAGE.

    Originally written by Christopher Wang aka Akiba.
    Please post support questions to the FreakLabs forum.

*******************************************************************/
/*!
    \file Cmd.c

    This implements a simple command line interface for the Arduino so that
    its possible to execute individual functions within the sketch. 
*/
/**************************************************************************/
#include "cmdArduino.h"

// command line message buffer and pointer
static uint8_t msg[MAX_MSG_SIZE];
static uint8_t *msg_ptr;

// linked list for command table
static cmd_t *cmd_tbl_list, *cmd_tbl;

// text strings for command prompt (stored in flash)
const char cmd_banner[] PROGMEM = "*************** CMD *******************";
const char cmd_prompt[] PROGMEM = "CMD >> ";
const char cmd_unrecog[] PROGMEM = "CMD: Command not recognized.";

Cmd cmd;

/**************************************************************************/
/*!
    constructor
*/
/**************************************************************************/
Cmd::Cmd()
{

}

/**************************************************************************/
/*!
    Generate the main command prompt
*/
/**************************************************************************/
void Cmd::display_prompt()
{
    char buf[50];

    _ser->println();

    //strcpy_P(buf, cmd_banner);
    //_ser->println(buf);

    strcpy_P(buf, cmd_prompt);
    _ser->print(buf);
}

/**************************************************************************/
/*!
    Parse the command line. This function tokenizes the command input, then
    searches for the command table entry associated with the commmand. Once found,
    it will jump to the corresponding function.
*/
/**************************************************************************/
void Cmd::parse(char *cmd)
{
    uint8_t argc, i = 0;
    char *argv[30];
    char buf[50];
    cmd_t *cmd_entry;

    fflush(stdout);

    // parse the command line statement and break it up into space-delimited
    // strings. the array of strings will be saved in the argv array.
    argv[i] = strtok(cmd, " ");
    do
    {
        argv[++i] = strtok(NULL, " ");
    } while ((i < 30) && (argv[i] != NULL));
    
    // save off the number of arguments for the particular command.
    argc = i;

    // parse the command table for valid command. used argv[0] which is the
    // actual command name typed in at the prompt
    for (cmd_entry = cmd_tbl; cmd_entry != NULL; cmd_entry = cmd_entry->next)
    {
        if (!strcmp(argv[0], cmd_entry->cmd))
        {
            cmd_entry->func(argc, argv);
            display_prompt();
            return;
        }
    }

    if(strlen(argv[0]) > 0) {
      // Command not recognized. Print message and re-generate prompt.
      strcpy_P(buf, cmd_unrecog);
      _ser->print(buf);
      _ser->print(" '");
      _ser->print(argv[0]);
      _ser->println("'");
    }
    
    display_prompt();
}

/**************************************************************************/
/*!
    This function processes the individual characters typed into the command
    prompt. It saves them off into the message buffer unless its a "backspace"
    or "enter" key. 
*/
/**************************************************************************/
void Cmd::handler()
{
    char c = _ser->read();

    switch (c)
    {
    case '\r':
        // Terminate the msg and reset the msg ptr. Then send
        // it to the handler for processing.
        *msg_ptr = '\0';
        _ser->print("\r\n");
        parse((char *)msg);
        msg_ptr = msg;
        break;
    
    case '\b':
    case 127: // 127 is delete which may be used by e.g. Putty as backspace
        // backspace 
        if (msg_ptr > msg)
        {
            _ser->print(c); // Do not allow deleting further back than the initial position after the prompt.
            *msg_ptr = ' ';
            msg_ptr--;
        }
        break;
    
    default:
        // normal character entered. add it to the buffer
        _ser->print(c);
        *msg_ptr++ = c;

        // msg too long, clear command and display warning 
        if ((msg_ptr - msg) == (MAX_MSG_SIZE-1))
        {
            _ser->println("Command too long. Please reduce command size.");
            msg_ptr = msg;
        }
        break;
    }
}

/**************************************************************************/
/*!
    This function should be set inside the main loop. It needs to be called
    constantly to check if there is any available input at the command prompt.
*/
/**************************************************************************/
void Cmd::poll()
{
    while (_ser->available())
    {
        handler();
    }
}

/**************************************************************************/
/*!
    Initialize the command line interface. This sets the terminal speed and
    and initializes things. 
*/
/**************************************************************************/
void Cmd::begin(uint32_t speed, HardwareSerial *ser)
{
    // init the msg ptr
    msg_ptr = msg;

    // init the command table
    cmd_tbl_list = NULL;

    // load in the serial pointer if it's passed in
    if (ser == NULL)
    {
        _ser = &Serial;
    }
    else
    {
        _ser = ser;
    }

    // set the serial speed
    ser->begin(speed);
}

/**************************************************************************/
/*!
    Add a command to the command table. The commands should be added in
    at the setup() portion of the sketch. 
*/
/**************************************************************************/
void Cmd::add(const char *name, void (*func)(int argc, char **argv))
{
    // alloc memory for command struct
    cmd_tbl = (cmd_t *)malloc(sizeof(cmd_t));

    // alloc memory for command name
    char *cmd_name = (char *)malloc(strlen(name)+1);

    // copy command name
    strcpy(cmd_name, name);

    // terminate the command name
    cmd_name[strlen(name)] = '\0';

    // fill out structure
    cmd_tbl->cmd = cmd_name;
    cmd_tbl->func = func;
    cmd_tbl->next = cmd_tbl_list;
    cmd_tbl_list = cmd_tbl;
}

/**************************************************************************/
/*!
    Convert a string to a number. The base must be specified, ie: "32" is a
    different value in base 10 (decimal) and base 16 (hexadecimal).
*/
/**************************************************************************/
uint32_t Cmd::conv(char *str, uint8_t base)
{
    return strtol(str, NULL, base);
}
