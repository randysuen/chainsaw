/*
    Copyright (C) 1999-2008 by Mark D. Hill and David A. Wood for the
    Wisconsin Multifacet Project.  Contact: gems@cs.wisc.edu
    http://www.cs.wisc.edu/gems/

    --------------------------------------------------------------------

    This file is part of Orion (Princeton's interconnect power model),
    a component of the Multifacet GEMS (General Execution-driven 
    Multiprocessor Simulator) software toolset originally developed at 
    the University of Wisconsin-Madison.

    Garnet was developed by Niket Agarwal at Princeton University. Orion was
    developed by Princeton University.

    Substantial further development of Multifacet GEMS at the
    University of Wisconsin was performed by Alaa Alameldeen, Brad
    Beckmann, Jayaram Bobba, Ross Dickson, Dan Gibson, Pacia Harper,
    Derek Hower, Milo Martin, Michael Marty, Carl Mauer, Michelle Moravan,
    Kevin Moore, Andrew Phelps, Manoj Plakal, Daniel Sorin, Haris Volos, 
    Min Xu, and Luke Yen.
    --------------------------------------------------------------------

    If your use of this software contributes to a published paper, we
    request that you (1) cite our summary paper that appears on our
    website (http://www.cs.wisc.edu/gems/) and (2) e-mail a citation
    for your published paper to gems@cs.wisc.edu.

    If you redistribute derivatives of this software, we request that
    you notify us and either (1) ask people to register with us at our
    website (http://www.cs.wisc.edu/gems/) or (2) collect registration
    information and periodically send it to us.

    --------------------------------------------------------------------

    Multifacet GEMS is free software; you can redistribute it and/or
    modify it under the terms of version 2 of the GNU General Public
    License as published by the Free Software Foundation.

    Multifacet GEMS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with the Multifacet GEMS; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
    02111-1307, USA

    The GNU General Public License is contained in the file LICENSE.

### END HEADER ###
*/
#include <stdio.h>

#include "power_arbiter.h"
#include "power_array.h"
#include "power_ll.h"
#include "parm_technology.h"
#include "SIM_port.h"
#include "power_static.h"
#include "power_utils.h"





/******************************* Power model for flip flop *****************************/

/* ------- flip flop model ---------- */

/* this model is based on the gate-level design given by Randy H. Katz "Contemporary Logic Design"
 * Figure 6.24, node numbers (1-6) are assigned to all gate outputs, left to right, top to bottom
 * 
 * We should have pure cap functions and leave the decision of whether or not to have coefficient
 * 1/2 in init function.
 */
static double SIM_fpfp_node_cap(unsigned fan_in, unsigned fan_out)
{
  double Ctotal = 0;

  /* FIXME: all need actual sizes */
  /* part 1: drain cap of NOR gate */
  Ctotal += fan_in * SIM_power_draincap(WdecNORn, NCH, 1) + SIM_power_draincap(WdecNORp, PCH, fan_in);

  /* part 2: gate cap of NOR gates */
  Ctotal += fan_out * SIM_power_gatecap(WdecNORn + WdecNORp, 0);

  return Ctotal;
}


static double SIM_fpfp_clock_cap(void)
{
  /* gate cap of clock load */
  return (2 * SIM_power_gatecap(WdecNORn + WdecNORp, 0));
}


int SIM_fpfp_clear_stat(power_ff *ff)
{
  ff->n_switch = ff->n_keep_1 = ff->n_keep_0 = ff->n_clock = 0;

  return 0;
}


int SIM_fpfp_init(power_ff *ff, int model, double load)
{
  double c1, c2, c3, c4, c5, c6;
  
  if ((ff->model = model) && model < FF_MAX_MODEL) {
    switch (model) {
      case NEG_DFF:
	   SIM_fpfp_clear_stat(ff);

           /* node 5 and node 6 are identical to node 1 in capacitance */
	   c1 = c5 = c6 = SIM_fpfp_node_cap(2, 1);
	   c2 = SIM_fpfp_node_cap(2, 3);
	   c3 = SIM_fpfp_node_cap(3, 2);
	   c4 = SIM_fpfp_node_cap(2, 3);

	   ff->e_switch = (c4 + c1 + c2 + c3 + c5 + c6 + load) / 2 * EnergyFactor;
	   /* no 1/2 for e_keep and e_clock because clock signal switches twice in one cycle */
	   ff->e_keep_1 = c3 * EnergyFactor; 
	   ff->e_keep_0 = c2 * EnergyFactor; 
	   ff->e_clock = SIM_fpfp_clock_cap() * EnergyFactor;

	   /* static power */
	   ff->i_leakage = (WdecNORp * NOR2_TAB[0] + WdecNORn * (NOR2_TAB[1] + NOR2_TAB[2] + NOR2_TAB[3])) / 4 * 6 / PARM_TECH_POINT * 100;
	   break;

      default:	break;/* some error handler */
    }

    return 0;
  }
  else
    return -1;
}


double SIM_fpfp_report(power_ff *ff)
{
  return (ff->e_switch * ff->n_switch + ff->e_clock * ff->n_clock +
	  ff->e_keep_0 * ff->n_keep_0 + ff->e_keep_1 * ff->n_keep_1);
}

/* ------- flip flop model ---------- */





/* -------- arbiter power model ------------- */

/* switch cap of request signal (round robin arbiter) */
static double rr_arbiter_req_cap(double length)
{
  double Ctotal = 0;

  /* part 1: gate cap of 2 NOR gates */
  /* FIXME: need actual size */
  Ctotal += 2 * SIM_power_gatecap(WdecNORn + WdecNORp, 0);

  /* part 2: inverter */
  /* FIXME: need actual size */
  Ctotal += SIM_power_draincap(Wdecinvn, NCH, 1) + SIM_power_draincap(Wdecinvp, PCH, 1) +
	    SIM_power_gatecap(Wdecinvn + Wdecinvp, 0);

  /* part 3: wire cap */
  Ctotal += length * Cmetal;

  return Ctotal;
}


/* switch cap of priority signal (round robin arbiter) */
static double rr_arbiter_pri_cap()
{
  double Ctotal = 0;

  /* part 1: gate cap of NOR gate */
  /* FIXME: need actual size */
  Ctotal += SIM_power_gatecap(WdecNORn + WdecNORp, 0);

  return Ctotal;
}


/* switch cap of grant signal (round robin arbiter) */
static double rr_arbiter_grant_cap()
{
  double Ctotal = 0;

  /* part 1: drain cap of NOR gate */
  /* FIXME: need actual size */
  Ctotal += 2 * SIM_power_draincap(WdecNORn, NCH, 1) + SIM_power_draincap(WdecNORp, PCH, 2);

  return Ctotal;
}


/* switch cap of carry signal (round robin arbiter) */
static double rr_arbiter_carry_cap()
{
  double Ctotal = 0;

  /* part 1: drain cap of NOR gate (this block) */
  /* FIXME: need actual size */
  Ctotal += 2 * SIM_power_draincap(WdecNORn, NCH, 1) + SIM_power_draincap(WdecNORp, PCH, 2);

  /* part 2: gate cap of NOR gate (next block) */
  /* FIXME: need actual size */
  Ctotal += SIM_power_gatecap(WdecNORn + WdecNORp, 0);

  return Ctotal;
}


/* switch cap of internal carry node (round robin arbiter) */
static double rr_arbiter_carry_in_cap()
{
  double Ctotal = 0;

  /* part 1: gate cap of 2 NOR gates */
  /* FIXME: need actual size */
  Ctotal += 2 * SIM_power_gatecap(WdecNORn + WdecNORp, 0);

  /* part 2: drain cap of NOR gate */
  /* FIXME: need actual size */
  Ctotal += 2 * SIM_power_draincap(WdecNORn, NCH, 1) + SIM_power_draincap(WdecNORp, PCH, 2);

  return Ctotal;
}


/* the "huge" NOR gate in matrix arbiter model is an approximation */
/* switch cap of request signal (matrix arbiter) */
static double matrix_arbiter_req_cap(unsigned req_width, double length)
{
  double Ctotal = 0;

  /* FIXME: all need actual sizes */
  /* part 1: gate cap of NOR gates */
  Ctotal += (req_width - 1) * SIM_power_gatecap(WdecNORn + WdecNORp, 0);
  
  /* part 2: inverter */
  Ctotal += SIM_power_draincap(Wdecinvn, NCH, 1) + SIM_power_draincap(Wdecinvp, PCH, 1) +
	    SIM_power_gatecap(Wdecinvn + Wdecinvp, 0);

  /* part 3: gate cap of the "huge" NOR gate */
  Ctotal += SIM_power_gatecap(WdecNORn + WdecNORp, 0);

  /* part 4: wire cap */
  Ctotal += length * Cmetal;

  return Ctotal;
}


/* switch cap of priority signal (matrix arbiter) */
static double matrix_arbiter_pri_cap(unsigned req_width)
{
  double Ctotal = 0;

  /* part 1: gate cap of NOR gates (2 groups) */
  Ctotal += 2 * SIM_power_gatecap(WdecNORn + WdecNORp, 0);

  /* no inverter because priority signal is kept by a flip flop */
  return Ctotal;
}


/* switch cap of grant signal (matrix arbiter) */
static double matrix_arbiter_grant_cap(unsigned req_width)
{
  /* drain cap of the "huge" NOR gate */
  return (req_width * SIM_power_draincap(WdecNORn, NCH, 1) + SIM_power_draincap(WdecNORp, PCH, req_width));
}


/* switch cap of internal node (matrix arbiter) */
static double matrix_arbiter_int_cap()
{
  double Ctotal = 0;
  
  /* part 1: drain cap of NOR gate */
  Ctotal += 2 * SIM_power_draincap(WdecNORn, NCH, 1) + SIM_power_draincap(WdecNORp, PCH, 2);

  /* part 2: gate cap of the "huge" NOR gate */
  Ctotal += SIM_power_gatecap(WdecNORn + WdecNORp, 0);

  return Ctotal;
}
  

static int arbiter_clear_stat(power_arbiter *arb)
{
  arb->n_chg_req = arb->n_chg_grant = arb->n_chg_mint = 0;
  arb->n_chg_carry = arb->n_chg_carry_in = 0;

  SIM_array_clear_stat(&arb->queue);
  SIM_fpfp_clear_stat(&arb->pri_ff);

  return 0;
}


int power_arbiter_init(power_arbiter *arb, int arbiter_model, int ff_model, unsigned req_width, double length, power_array_info *info)
{
  if ((arb->model = arbiter_model) && arbiter_model < ARBITER_MAX_MODEL) {
    arb->req_width = req_width;
    arbiter_clear_stat(arb);
    /* redundant field */
    arb->mask = HAMM_MASK(req_width);

    switch (arbiter_model) {
      case RR_ARBITER:
	   arb->e_chg_req = rr_arbiter_req_cap(length) / 2 * EnergyFactor;
	   /* two grant signals switch together, so no 1/2 */
	   arb->e_chg_grant = rr_arbiter_grant_cap() * EnergyFactor;
	   arb->e_chg_carry = rr_arbiter_carry_cap() / 2 * EnergyFactor;
	   arb->e_chg_carry_in = rr_arbiter_carry_in_cap() / 2 * EnergyFactor;
	   arb->e_chg_mint = 0;

	   if (SIM_fpfp_init(&arb->pri_ff, ff_model, rr_arbiter_pri_cap()))
	     return -1;
	   break;

      case MATRIX_ARBITER:
	   arb->e_chg_req = matrix_arbiter_req_cap(req_width, length) / 2 * EnergyFactor;
	   /* 2 grant signals switch together, so no 1/2 */
	   arb->e_chg_grant = matrix_arbiter_grant_cap(req_width) * EnergyFactor;
	   arb->e_chg_mint = matrix_arbiter_int_cap() / 2 * EnergyFactor;
	   arb->e_chg_carry = arb->e_chg_carry_in = 0;

	   if (SIM_fpfp_init(&arb->pri_ff, ff_model, matrix_arbiter_pri_cap(req_width)))
	     return -1;
	   break;

      case QUEUE_ARBITER:
	   arb->e_chg_req = arb->e_chg_grant = arb->e_chg_mint = 0;
	   arb->e_chg_carry = arb->e_chg_carry_in = 0;

	   return power_array_init(info, &arb->queue);
	   break;

      default:	break;/* some error handler */
    }

    return 0;
  }
  else
    return -1;
}


int arbiter_record(power_arbiter *arb, unsigned long int new_req, unsigned long int old_req, unsigned new_grant, unsigned old_grant)
{
  switch (arb->model) {
    case MATRIX_ARBITER:
	 arb->n_chg_req += SIM_power_Hamming(new_req, old_req, arb->mask);
	 arb->n_chg_grant += new_grant != old_grant;
	 /* FIXME: approximation */
	 arb->n_chg_mint += (arb->req_width - 1) * arb->req_width / 2;
	 /* priority registers */
	 /* FIXME: use average instead */
	 arb->pri_ff.n_switch += (arb->req_width - 1) / 2;
	 break;

    case RR_ARBITER:
	 arb->n_chg_req += SIM_power_Hamming(new_req, old_req, arb->mask);
	 arb->n_chg_grant += new_grant != old_grant;
	 /* FIXME: use average instead */
	 arb->n_chg_carry += arb->req_width / 2;
	 arb->n_chg_carry_in += arb->req_width / 2 - 1;
	 /* priority registers */
	 arb->pri_ff.n_switch += 2;
	 break;

    case QUEUE_ARBITER:
	 break;

    default:	break;/* some error handler */
  }

  return 0;
}


double arbiter_report(power_arbiter *arb)
{
  switch (arb->model) {
    case MATRIX_ARBITER:
	 return (arb->n_chg_req * arb->e_chg_req + arb->n_chg_grant * arb->e_chg_grant +
		 arb->n_chg_mint * arb->e_chg_mint +
		 arb->pri_ff.n_switch * arb->pri_ff.e_switch +
		 arb->pri_ff.n_keep_1 * arb->pri_ff.e_keep_1 +
		 arb->pri_ff.n_keep_0 * arb->pri_ff.e_keep_0 +
		 arb->pri_ff.n_clock * arb->pri_ff.e_clock);

    case RR_ARBITER:
	 return (arb->n_chg_req * arb->e_chg_req + arb->n_chg_grant * arb->e_chg_grant +
		 arb->n_chg_carry * arb->e_chg_carry + arb->n_chg_carry_in * arb->e_chg_carry_in +
		 arb->pri_ff.n_switch * arb->pri_ff.e_switch +
		 arb->pri_ff.n_keep_1 * arb->pri_ff.e_keep_1 +
		 arb->pri_ff.n_keep_0 * arb->pri_ff.e_keep_0 +
		 arb->pri_ff.n_clock * arb->pri_ff.e_clock);

    default: return -1;
  }
}

/* ---------- arbiter power model ----------- */


