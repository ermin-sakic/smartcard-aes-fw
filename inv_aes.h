/*     
       A generic SmartCard firmware allowing for communication based 
       on ISO 7816 Part 3/Part 4 protocol standards, incorporating 
       safe AES-128 with masking and shuffling for decryption purposes.

       Authors:: Ermin Sakic, Thomas Wohlfahrt
 
       Licensed to the Apache Software Foundation (ASF) under one
       or more contributor license agreements.  See the NOTICE file
       distributed with this work for additional information
       regarding copyright ownership.  The ASF licenses this file
       to you under the Apache License, Version 2.0 (the
       "License"); you may not use this file except in compliance
       with the License.  You may obtain a copy of the License at

         http://www.apache.org/licenses/LICENSE-2.0

       Unless required by applicable law or agreed to in writing,
       software distributed under the License is distributed on an
       "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
       KIND, either express or implied.  See the License for the
       specific language governing permissions and limitations
       under the License.
*/

#ifndef INV_AES
#define INV_AES

#include <avr/io.h>
#include <inttypes.h>

/**
 AES Implementation With Masking:                                                        
*/



//Funktions:


/************************************************************************/
/* Look                                                                     */
/************************************************************************/
void inv_mixColumns(uint8_t state[16]);
void inv_shiftRows(uint8_t state[16]);
void inv_aes128(uint8_t state[16]);

void init_masking();
void remask(uint8_t s[16], uint8_t m1, uint8_t m2, uint8_t m3, uint8_t m4, uint8_t m5, uint8_t m6, uint8_t m7, uint8_t m8);
void inv_subBytes_masked(uint8_t state[16]);
void init_masked_round_keys();
void calcInvSbox_masked();
void calcMixColMask();
void addRoundKey_masked(uint8_t state[16],uint8_t round);
void copy_key();

void gen_random_sequence(uint8_t hiding_sequence[16]);
void inv_subBytes_masked_rand(uint8_t state[16],uint8_t hiding_sequence[16]);
#endif

