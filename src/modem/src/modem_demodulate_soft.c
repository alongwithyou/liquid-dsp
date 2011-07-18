/*
 * Copyright (c) 2007, 2008, 2009, 2010, 2011 Joseph Gaeddert
 * Copyright (c) 2007, 2008, 2009, 2010, 2011 Virginia Polytechnic
 *                                      Institute & State University
 *
 * This file is part of liquid.
 *
 * liquid is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * liquid is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with liquid.  If not, see <http://www.gnu.org/licenses/>.
 */

//
// modem_demodulate_soft.c
//
// Definitions for linear soft demodulation of symbols.
//

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>

#include "liquid.internal.h"

#define DEBUG_DEMODULATE_SOFT 0

// generic demodulation
void modem_demodulate_soft(modem _demod,
                          float complex _x,
                          unsigned int  * _s,
                          unsigned char * _soft_bits)
{
    // switch scheme
    switch (_demod->scheme) {
    case LIQUID_MODEM_ARB:      modem_demodulate_soft_arb( _demod,_x,_s,_soft_bits); return;
    case LIQUID_MODEM_BPSK:     modem_demodulate_soft_bpsk(_demod,_x,_s,_soft_bits); return;
    case LIQUID_MODEM_QPSK:     modem_demodulate_soft_qpsk(_demod,_x,_s,_soft_bits); return;
    default:;
    }

    // check if...
    if (_demod->demod_soft_neighbors != NULL && _demod->demod_soft_p != 0) {
        // demodulate using approximate log-likelihood method with
        // look-up table for nearest neighbors
        modem_demodulate_soft_table(_demod, _x, _s, _soft_bits);

        return;
    }

    // for now demodulate normally and simply copy the
    // hard-demodulated bits
    unsigned int symbol_out;
    _demod->demodulate_func(_demod, _x, &symbol_out);

    unsigned int i;
    for (i=0; i<_demod->m; i++)
        _soft_bits[i] = ((symbol_out >> (_demod->m-i-1)) & 0x0001) ? LIQUID_FEC_SOFTBIT_1 : LIQUID_FEC_SOFTBIT_0;

    *_s = symbol_out;
}

#if DEBUG_DEMODULATE_SOFT
// print a string of bits to the standard output
void print_bitstring_demod_soft(unsigned int _x,
                     unsigned int _n)
{
    unsigned int i;
    for (i=0; i<_n; i++)
        printf("%1u", (_x >> (_n-i-1)) & 1);
}
#endif

// generic soft demodulation using look-up table...
//  _demod      :   demodulator object
//  _r          :   received sample
//  _s          :   hard demodulator output
//  _soft_bits  :   soft bit ouput (approximate log-likelihood ratio)
void modem_demodulate_soft_table(modem _demod,
                                 float complex _r,
                                 unsigned int * _s,
                                 unsigned char * _soft_bits)
{
#if DEBUG_DEMODULATE_SOFT
    printf("\nmodem_demodulate_soft_table() invoked\n");
#endif
    // run hard demodulation; this will store re-modulated sample
    // as internal variable x_hat
    unsigned int s;
    modem_demodulate(_demod, _r, &s);
#if DEBUG_DEMODULATE_SOFT
    printf("  hard demod    :   %3u\n", *_s);
#endif

    unsigned int bps = modem_get_bps(_demod);

    float sig = 0.2f;

    unsigned int k;
    unsigned int i;
    unsigned int bit;
    float d;
    float complex x_hat;    // re-modulated symbol
    unsigned char * softab = _demod->demod_soft_neighbors;
    unsigned int p = _demod->demod_soft_p;
    for (k=0; k<bps; k++) {
        // initialize soft bit value
        _soft_bits[k] = 127;

        // find nearest 0 and nearest 1
        float dmin_0 = 1.0f;
        float dmin_1 = 1.0f;

        // check bit of hard demodulation
        d = crealf( (_r-_demod->x_hat)*conjf(_r-_demod->x_hat) );
        bit = (s >> (bps-k-1)) & 0x01;
        if (bit) dmin_1 = d;
        else     dmin_0 = d;

        // check symbols in table
#if DEBUG_DEMODULATE_SOFT
        printf("  index %2u : ", k);
#endif
        for (i=0; i<p; i++) {
            bit = (softab[s*p+i] >> (bps-k-1)) & 0x01;

#if DEBUG_DEMODULATE_SOFT
            print_bitstring_demod_soft(softab[s*p+i],bps);
            printf("[%1u]", bit);
#endif

            // compute distance by re-modulating symbol...
            if (_demod->modulate_using_map)
                x_hat = _demod->symbol_map[ softab[s*p + i] ];
            else
                modem_modulate(_demod, softab[s*p+i], &x_hat);
            d = crealf( (_r-x_hat)*conjf(_r-x_hat) );
#if DEBUG_DEMODULATE_SOFT
            printf("(%8.6f) ", d);
#endif
            if (bit) {
                if (d < dmin_1) dmin_1 = d;
            } else {
                if (d < dmin_0) dmin_0 = d;
            }
        }
#if DEBUG_DEMODULATE_SOFT
        printf("\n");
        printf("  dmin_0 : %12.8f\n", dmin_0);
        printf("  dmin_1 : %12.8f\n", dmin_1);
#endif

        // make assignments
        int soft_bit = ((-dmin_1/(2.0f*sig*sig)) - (-dmin_0/(2.0f*sig*sig)))*16 + 127;
        if (soft_bit > 255) soft_bit = 255;
        if (soft_bit <   0) soft_bit = 0;
        _soft_bits[k] = (unsigned char)soft_bit;
    }

    // set hard output symbol
    *_s = s;
}



// demodulate arbitrary modem type (soft)
void modem_demodulate_soft_arb(modem _demod,
                               float complex _r,
                               unsigned int  * _s,
                               unsigned char * _soft_bits)
{
    unsigned int bps = _demod->m;
    unsigned int M   = _demod->M;

    // TODO : compute sig based on minimum distance between symbols
    float sig = 0.2f;

    unsigned int s=0;       // hard decision output
    unsigned int k;         // bit index
    unsigned int i;         // symbol index
    float d;                // distance for this symbol
    float complex x_hat;    // re-modulated symbol

    float dmin_0[bps];
    float dmin_1[bps];
    for (k=0; k<bps; k++) {
        dmin_0[k] = 4.0f;
        dmin_1[k] = 4.0f;
    }
    float dmin = 0.0f;

    for (i=0; i<M; i++) {
        // compute distance from received symbol
        x_hat = _demod->symbol_map[i];
        d = crealf( (_r-x_hat)*conjf(_r-x_hat) );

        // set hard-decision...
        if (d < dmin || i==0) {
            s = i;
            dmin = d;
        }

        for (k=0; k<bps; k++) {
            // strip bit
            if ( (s >> (bps-k-1)) & 0x01 ) {
                if (d < dmin_1[k]) dmin_1[k] = d;
            } else {
                if (d < dmin_0[k]) dmin_0[k] = d;
            }
        }
    }

    // make assignments
    for (k=0; k<bps; k++) {
        int soft_bit = ((-dmin_1[k]/(2.0f*sig*sig)) - (-dmin_0[k]/(2.0f*sig*sig)))*16 + 127;
        if (soft_bit > 255) soft_bit = 255;
        if (soft_bit <   0) soft_bit = 0;
        _soft_bits[k] = (unsigned char)soft_bit;
    }

    // hard decision

    // set hard output symbol
    *_s = s;

    // re-modulate symbol and store state
    modem_modulate_arb(_demod, *_s, &_demod->x_hat);
    _demod->r = _r;
}

// demodulate BPSK (soft)
void modem_demodulate_soft_bpsk(modem _demod,
                                float complex _x,
                                unsigned int  * _s,
                                unsigned char * _soft_bits)
{
    // soft output
    _soft_bits[0] = (unsigned char) ( 255*(0.5 + 0.5*tanhf(crealf(_x))) );

    // re-modulate symbol and store state
    unsigned int symbol_out = (crealf(_x) > 0 ) ? 0 : 1;
    modem_modulate_bpsk(_demod, symbol_out, &_demod->x_hat);
    _demod->r = _x;
    *_s = symbol_out;
}

// demodulate QPSK (soft)
void modem_demodulate_soft_qpsk(modem _demod,
                                float complex _x,
                                unsigned int  * _s,
                                unsigned char * _soft_bits)
{
    // soft output
    _soft_bits[0] = (unsigned char) ( 255*(0.5 + 0.5*tanhf(1.4142*crealf(_x))) );
    _soft_bits[1] = (unsigned char) ( 255*(0.5 + 0.5*tanhf(1.4142*cimagf(_x))) );

    // re-modulate symbol and store state
    unsigned int symbol_out  = (crealf(_x) > 0 ? 0 : 1) +
                               (cimagf(_x) > 0 ? 0 : 2);
    modem_modulate_qpsk(_demod, symbol_out, &_demod->x_hat);
    _demod->r = _x;
    *_s = symbol_out;
}

