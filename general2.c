/* GENERAL2.C   (c) Copyright Roger Bowler, 1994-2005                */
/*              ESA/390 CPU Emulator                                 */
/*              Instructions N-Z                                     */

/*              (c) Copyright Peter Kuschnerus, 1999-2005 (UPT & CFC)*/

/* Interpretive Execution - (c) Copyright Jan Jaeger, 1999-2005      */
/* z/Architecture support - (c) Copyright Jan Jaeger, 1999-2005      */

/*-------------------------------------------------------------------*/
/* This module implements all general instructions of the            */
/* S/370 and ESA/390 architectures, as described in the manuals      */
/* GA22-7000-03 System/370 Principles of Operation                   */
/* SA22-7201-06 ESA/390 Principles of Operation                      */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* Additional credits:                                               */
/*      Corrections to shift instructions by Jay Maynard, Jan Jaeger */
/*      Branch tracing by Jan Jaeger                                 */
/*      Instruction decode by macros - Jan Jaeger                    */
/*      Prevent TOD from going backwards in time - Jan Jaeger        */
/*      Fix STCKE instruction - Bernard van der Helm                 */
/*      Instruction decode rework - Jan Jaeger                       */
/*      Make STCK update the TOD clock - Jay Maynard                 */
/*      Fix address wraparound in MVO - Jan Jaeger                   */
/*      PLO instruction - Jan Jaeger                                 */
/*      Modifications for Interpretive Execution (SIE) by Jan Jaeger */
/*      Clear TEA on data exception - Peter Kuschnerus           v209*/
/*-------------------------------------------------------------------*/

#include "hstdinc.h"

#include "hercules.h"
#include "opcode.h"
#include "inline.h"


/*-------------------------------------------------------------------*/
/* 16   OR    - Or Register                                     [RR] */
/*-------------------------------------------------------------------*/
DEF_INST(or_register)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, regs, r1, r2);

    /* OR second operand with first and set condition code */
    regs->psw.cc = ( regs->GR_L(r1) |= regs->GR_L(r2) ) ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* 56   O     - Or                                              [RX] */
/*-------------------------------------------------------------------*/
DEF_INST(or)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */

    RX(inst, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = ARCH_DEP(vfetch4) ( effective_addr2, b2, regs );

    /* OR second operand with first and set condition code */
    regs->psw.cc = ( regs->GR_L(r1) |= n ) ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* 96   OI    - Or Immediate                                    [SI] */
/*-------------------------------------------------------------------*/
DEF_INST(or_immediate)
{
BYTE    i2;                             /* Immediate operand byte    */
int     b1;                             /* Base of effective addr    */
VADR    effective_addr1;                /* Effective address         */
BYTE   *dest;                           /* Pointer to target byte    */

    SI(inst, regs, i2, b1, effective_addr1);

    /* Get byte mainstor address */
    dest = MADDR (effective_addr1, b1, regs, ACCTYPE_WRITE, regs->psw.pkey );

    /* OR byte with immediate operand, setting condition code */
    regs->psw.cc = ((*dest |= i2) != 0);
}


/*-------------------------------------------------------------------*/
/* D6   OC    - Or Characters                                   [SS] */
/*-------------------------------------------------------------------*/
DEF_INST(or_character)
{
int     len, len2, len3;                /* Lengths to copy           */
int     b1, b2;                         /* Base register numbers     */
VADR    addr1, addr2;                   /* Virtual addresses         */
BYTE   *dest1, *dest2;                  /* Destination addresses     */
BYTE   *source1, *source2;              /* Source addresses          */
BYTE   *sk1, *sk2;                      /* Storage key addresses     */
int     i;                              /* Loop counter              */
int     cc = 0;                         /* Condition code            */

    SS_L(inst, regs, len, b1, addr1, b2, addr2);

    /* Quick out for 1 byte (no boundary crossed) */
    if (unlikely(len == 0))
    {
        source1 = MADDR (addr2, b2, regs, ACCTYPE_READ, regs->psw.pkey);
        dest1 = MADDR (addr1, b1, regs, ACCTYPE_WRITE, regs->psw.pkey);
        *dest1 |= *source1;
        regs->psw.cc = (*dest1 != 0);
        return;
    }

    /* There are several scenarios (in optimal order):
     * (1) dest boundary and source boundary not crossed
     * (2) dest boundary not crossed and source boundary crossed
     * (3) dest boundary crossed and source boundary not crossed
     * (4) dest boundary and source boundary are crossed
     *     (a) dest and source boundary cross at the same time
     *     (b) dest boundary crossed first
     *     (c) source boundary crossed first
     */

    /* Translate addresses of leftmost operand bytes */
    dest1 = MADDR (addr1, b1, regs, ACCTYPE_WRITE_SKP, regs->psw.pkey);
    sk1 = regs->dat.storkey;
    source1 = MADDR (addr2, b2, regs, ACCTYPE_READ, regs->psw.pkey);

    if ( NOCROSS2K(addr1,len) )
    {
        if ( NOCROSS2K(addr2,len) )
        {
            /* (1) - No boundaries are crossed */
            for (i = 0; i <= len; i++)
                if (*dest1++ |= *source1++) cc = 1;

        }
        else
        {
             /* (2) - Second operand crosses a boundary */
             len2 = 0x800 - (addr2 & 0x7FF);
             source2 = MADDR ((addr2 + len2) & ADDRESS_MAXWRAP(regs),
                              b2, regs, ACCTYPE_READ, regs->psw.pkey);
             for ( i = 0; i < len2; i++)
                 if (*dest1++ |= *source1++) cc = 1;
             len2 = len - len2;
             for ( i = 0; i <= len2; i++)
                 if (*dest1++ |= *source2++) cc = 1;
        }
        *sk1 |= (STORKEY_REF | STORKEY_CHANGE);
    }
    else
    {
        /* First operand crosses a boundary */
        len2 = 0x800 - (addr1 & 0x7FF);
        dest2 = MADDR ((addr1 + len2) & ADDRESS_MAXWRAP(regs),
                       b1, regs, ACCTYPE_WRITE_SKP, regs->psw.pkey);
        sk2 = regs->dat.storkey;

        if ( NOCROSS2K(addr2,len) )
        {
             /* (3) - First operand crosses a boundary */
             for ( i = 0; i < len2; i++)
                 if (*dest1++ |= *source1++) cc = 1;
             len2 = len - len2;
             for ( i = 0; i <= len2; i++)
                 if (*dest2++ |= *source1++) cc = 1;
        }
        else
        {
            /* (4) - Both operands cross a boundary */
            len3 = 0x800 - (addr2 & 0x7FF);
            source2 = MADDR ((addr2 + len3) & ADDRESS_MAXWRAP(regs),
                             b2, regs, ACCTYPE_READ, regs->psw.pkey);
            if (len2 == len3)
            {
                /* (4a) - Both operands cross at the same time */
                for ( i = 0; i < len2; i++)
                    if (*dest1++ |= *source1++) cc = 1;
                len2 = len - len2;
                for ( i = 0; i <= len2; i++)
                    if (*dest2++ |= *source2++) cc = 1;
            }
            else if (len2 < len3)
            {
                /* (4b) - First operand crosses first */
                for ( i = 0; i < len2; i++)
                    if (*dest1++ |= *source1++) cc = 1;
                len2 = len3 - len2;
                for ( i = 0; i < len2; i++)
                    if (*dest2++ |= *source1++) cc = 1;
                len2 = len - len3;
                for ( i = 0; i <= len2; i++)
                    if (*dest2++ |= *source2++) cc = 1;
            }
            else
            {
                /* (4c) - Second operand crosses first */
                for ( i = 0; i < len3; i++)
                    if (*dest1++ |= *source1++) cc = 1;
                len3 = len2 - len3;
                for ( i = 0; i < len3; i++)
                    if (*dest1++ |= *source2++) cc = 1;
                len3 = len - len2;
                for ( i = 0; i <= len3; i++)
                    if (*dest2++ |= *source2++) cc = 1;
            }
        }
        *sk1 |= (STORKEY_REF | STORKEY_CHANGE);
        *sk2 |= (STORKEY_REF | STORKEY_CHANGE);
    }

    regs->psw.cc = cc;

}


/*-------------------------------------------------------------------*/
/* F2   PACK  - Pack                                            [SS] */
/*-------------------------------------------------------------------*/
DEF_INST(pack)
{
int     l1, l2;                         /* Lenght values             */
int     b1, b2;                         /* Values of base registers  */
VADR    effective_addr1,
        effective_addr2;                /* Effective addresses       */
int     i, j;                           /* Loop counters             */
BYTE    sbyte;                          /* Source operand byte       */
BYTE    dbyte;                          /* Destination operand byte  */

    SS(inst, regs, l1, l2, b1, effective_addr1,
                                     b2, effective_addr2);

    /* If operand 1 crosses a page, make sure both pages are accessable */
    if((effective_addr1 & PAGEFRAME_PAGEMASK) !=
        ((effective_addr1 + l1) & PAGEFRAME_PAGEMASK))
        ARCH_DEP(validate_operand) (effective_addr1, b1, l1, ACCTYPE_WRITE_SKP, regs);

    /* If operand 2 crosses a page, make sure both pages are accessable */
    if((effective_addr2 & PAGEFRAME_PAGEMASK) !=
        ((effective_addr2 + l2) & PAGEFRAME_PAGEMASK))
        ARCH_DEP(validate_operand) (effective_addr2, b2, l2, ACCTYPE_READ, regs);

    /* Exchange the digits in the rightmost byte */
    effective_addr1 += l1;
    effective_addr2 += l2;
    sbyte = ARCH_DEP(vfetchb) ( effective_addr2, b2, regs );
    dbyte = (sbyte << 4) | (sbyte >> 4);
    ARCH_DEP(vstoreb) ( dbyte, effective_addr1, b1, regs );

    /* Process remaining bytes from right to left */
    for (i = l1, j = l2; i > 0; i--)
    {
        /* Fetch source bytes from second operand */
        if (j-- > 0)
        {
            sbyte = ARCH_DEP(vfetchb) ( --effective_addr2, b2, regs );
            dbyte = sbyte & 0x0F;

            if (j-- > 0)
            {
                effective_addr2 &= ADDRESS_MAXWRAP(regs);
                sbyte = ARCH_DEP(vfetchb) ( --effective_addr2, b2, regs );
                dbyte |= sbyte << 4;
            }
        }
        else
        {
            dbyte = 0;
        }

        /* Store packed digits at first operand address */
        ARCH_DEP(vstoreb) ( dbyte, --effective_addr1, b1, regs );

        /* Wraparound according to addressing mode */
        effective_addr1 &= ADDRESS_MAXWRAP(regs);
        effective_addr2 &= ADDRESS_MAXWRAP(regs);

    } /* end for(i) */

}


#if defined(FEATURE_PERFORM_LOCKED_OPERATION)
/*-------------------------------------------------------------------*/
/* EE   PLO   - Perform Locked Operation                        [SS] */
/*-------------------------------------------------------------------*/
DEF_INST(perform_locked_operation)
{
int     r1, r3;                         /* Lenght values             */
int     b2, b4;                         /* Values of base registers  */
VADR    effective_addr2,
        effective_addr4;                /* Effective addresses       */

    SS(inst, regs, r1, r3, b2, effective_addr2,
                                     b4, effective_addr4);

    if(regs->GR_L(0) & PLO_GPR0_RESV)
        ARCH_DEP(program_interrupt)(regs, PGM_SPECIFICATION_EXCEPTION);

    if(regs->GR_L(0) & PLO_GPR0_T)
        switch(regs->GR_L(0) & PLO_GPR0_FC)
    {
        case PLO_CL:
        case PLO_CLG:
        case PLO_CS:
        case PLO_CSG:
        case PLO_DCS:
        case PLO_DCSG:
        case PLO_CSST:
        case PLO_CSSTG:
        case PLO_CSDST:
        case PLO_CSDSTG:
        case PLO_CSTST:
        case PLO_CSTSTG:
#if defined(FEATURE_ESAME)
        case PLO_CLGR:
        case PLO_CLX:
        case PLO_CSGR:
        case PLO_CSX:
        case PLO_DCSGR:
        case PLO_DCSX:
        case PLO_CSSTGR:
        case PLO_CSSTX:
        case PLO_CSDSTGR:
        case PLO_CSDSTX:
        case PLO_CSTSTGR:
        case PLO_CSTSTX:
#endif /*defined(FEATURE_ESAME)*/

            /* Indicate function supported */
            regs->psw.cc = 0;
            break;

        default:
            /* indicate function not supported */
            regs->psw.cc = 3;
            break;
    }
    else
    {
        /* gpr1/ar1 indentify the program lock token, which is used
           to select a lock from the model dependent number of locks
           in the configuration.  We simply use 1 lock which is the
           main storage access lock which is also used by CS, CDS
           and TS.                                               *JJ */
        OBTAIN_MAINLOCK(regs);

        switch(regs->GR_L(0) & PLO_GPR0_FC)
        {
            case PLO_CL:
                regs->psw.cc = ARCH_DEP(plo_cl) (r1, r3,
                        effective_addr2, b2, effective_addr4, b4, regs);
                break;
            case PLO_CLG:
                regs->psw.cc = ARCH_DEP(plo_clg) (r1, r3,
                        effective_addr2, b2, effective_addr4, b4, regs);
                break;
            case PLO_CS:
                regs->psw.cc = ARCH_DEP(plo_cs) (r1, r3,
                        effective_addr2, b2, effective_addr4, b4, regs);
                break;
            case PLO_CSG:
                regs->psw.cc = ARCH_DEP(plo_csg) (r1, r3,
                        effective_addr2, b2, effective_addr4, b4, regs);
                break;
            case PLO_DCS:
                regs->psw.cc = ARCH_DEP(plo_dcs) (r1, r3,
                        effective_addr2, b2, effective_addr4, b4, regs);
                break;
            case PLO_DCSG:
                regs->psw.cc = ARCH_DEP(plo_dcsg) (r1, r3,
                        effective_addr2, b2, effective_addr4, b4, regs);
                break;
            case PLO_CSST:
                regs->psw.cc = ARCH_DEP(plo_csst) (r1, r3,
                        effective_addr2, b2, effective_addr4, b4, regs);
                break;
            case PLO_CSSTG:
                regs->psw.cc = ARCH_DEP(plo_csstg) (r1, r3,
                        effective_addr2, b2, effective_addr4, b4, regs);
                break;
            case PLO_CSDST:
                regs->psw.cc = ARCH_DEP(plo_csdst) (r1, r3,
                        effective_addr2, b2, effective_addr4, b4, regs);
                break;
            case PLO_CSDSTG:
                regs->psw.cc = ARCH_DEP(plo_csdstg) (r1, r3,
                        effective_addr2, b2, effective_addr4, b4, regs);
                break;
            case PLO_CSTST:
                regs->psw.cc = ARCH_DEP(plo_cstst) (r1, r3,
                        effective_addr2, b2, effective_addr4, b4, regs);
                break;
            case PLO_CSTSTG:
                regs->psw.cc = ARCH_DEP(plo_cststg) (r1, r3,
                        effective_addr2, b2, effective_addr4, b4, regs);
                break;
#if defined(FEATURE_ESAME)
            case PLO_CLGR:
                regs->psw.cc = ARCH_DEP(plo_clgr) (r1, r3,
                        effective_addr2, b2, effective_addr4, b4, regs);
                break;
            case PLO_CLX:
                regs->psw.cc = ARCH_DEP(plo_clx) (r1, r3,
                        effective_addr2, b2, effective_addr4, b4, regs);
                break;
            case PLO_CSGR:
                regs->psw.cc = ARCH_DEP(plo_clgr) (r1, r3,
                        effective_addr2, b2, effective_addr4, b4, regs);
                break;
            case PLO_CSX:
                regs->psw.cc = ARCH_DEP(plo_csx) (r1, r3,
                        effective_addr2, b2, effective_addr4, b4, regs);
                break;
            case PLO_DCSGR:
                regs->psw.cc = ARCH_DEP(plo_dcsgr) (r1, r3,
                        effective_addr2, b2, effective_addr4, b4, regs);
                break;
            case PLO_DCSX:
                regs->psw.cc = ARCH_DEP(plo_dcsx) (r1, r3,
                        effective_addr2, b2, effective_addr4, b4, regs);
                break;
            case PLO_CSSTGR:
                regs->psw.cc = ARCH_DEP(plo_csstgr) (r1, r3,
                        effective_addr2, b2, effective_addr4, b4, regs);
                break;
            case PLO_CSSTX:
                regs->psw.cc = ARCH_DEP(plo_csstx) (r1, r3,
                        effective_addr2, b2, effective_addr4, b4, regs);
                break;
            case PLO_CSDSTGR:
                regs->psw.cc = ARCH_DEP(plo_csdstgr) (r1, r3,
                        effective_addr2, b2, effective_addr4, b4, regs);
                break;
            case PLO_CSDSTX:
                regs->psw.cc = ARCH_DEP(plo_csdstx) (r1, r3,
                        effective_addr2, b2, effective_addr4, b4, regs);
                break;
            case PLO_CSTSTGR:
                regs->psw.cc = ARCH_DEP(plo_cststgr) (r1, r3,
                        effective_addr2, b2, effective_addr4, b4, regs);
                break;
            case PLO_CSTSTX:
                regs->psw.cc = ARCH_DEP(plo_cststx) (r1, r3,
                        effective_addr2, b2, effective_addr4, b4, regs);
                break;
#endif /*defined(FEATURE_ESAME)*/


            default:
                ARCH_DEP(program_interrupt)(regs, PGM_SPECIFICATION_EXCEPTION);

        }

        /* Release main-storage access lock */
        RELEASE_MAINLOCK(regs);

        if(regs->psw.cc && sysblk.cpus > 1)
            sched_yield();

    }
}
#endif /*defined(FEATURE_PERFORM_LOCKED_OPERATION)*/


/*-------------------------------------------------------------------*/
/* B25E SRST  - Search String                                  [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(search_string)
{
int     r1, r2;                         /* Values of R fields        */
int     i;                              /* Loop counter              */
VADR    addr1, addr2;                   /* End/start addresses       */
BYTE    sbyte;                          /* String character          */
BYTE    termchar;                       /* Terminating character     */

    RRE(inst, regs, r1, r2);

    /* Program check if bits 0-23 of register 0 not zero */
    if ((regs->GR_L(0) & 0xFFFFFF00) != 0)
        ARCH_DEP(program_interrupt) (regs, PGM_SPECIFICATION_EXCEPTION);

    /* Load string terminating character from register 0 bits 24-31 */
    termchar = regs->GR_LHLCL(0);

    /* Determine the operand end and start addresses */
    addr1 = regs->GR(r1) & ADDRESS_MAXWRAP(regs);
    addr2 = regs->GR(r2) & ADDRESS_MAXWRAP(regs);

    /* Search up to 256 bytes or until end of operand */
    for (i = 0; i < 0x100; i++)
    {
        /* If operand end address has been reached, return condition
           code 2 and leave the R1 and R2 registers unchanged */
        if (addr2 == addr1)
        {
            regs->psw.cc = 2;
            return;
        }

        /* Fetch a byte from the operand */
        sbyte = ARCH_DEP(vfetchb) ( addr2, r2, regs );

        /* If the terminating character was found, return condition
           code 1 and load the address of the character into R1 */
        if (sbyte == termchar)
        {
            SET_GR_A(r1, regs, addr2);
            regs->psw.cc = 1;
            return;
        }

        /* Increment operand address */
        addr2++;
        addr2 &= ADDRESS_MAXWRAP(regs);

    } /* end for(i) */

    /* Set R2 to point to next character of operand */
    SET_GR_A(r2, regs, addr2);

    /* Return condition code 3 */
    regs->psw.cc = 3;

}


#if defined(FEATURE_ACCESS_REGISTERS)
/*-------------------------------------------------------------------*/
/* B24E SAR   - Set Access Register                            [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(set_access_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, regs, r1, r2);

    /* Copy R2 general register to R1 access register */
    regs->AR(r1) = regs->GR_L(r2);
    SET_AEA_AR(regs, r1);
}
#endif /*defined(FEATURE_ACCESS_REGISTERS)*/


/*-------------------------------------------------------------------*/
/* 04   SPM   - Set Program Mask                                [RR] */
/*-------------------------------------------------------------------*/
DEF_INST(set_program_mask)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, regs, r1, r2);

    /* Set condition code from bits 2-3 of R1 register */
    regs->psw.cc = ( regs->GR_L(r1) & 0x30000000 ) >> 28;

    /* Set program mask from bits 4-7 of R1 register */
    regs->psw.progmask = ( regs->GR_L(r1) >> 24) & PSW_PROGMASK;
}


/*-------------------------------------------------------------------*/
/* 8F   SLDA  - Shift Left Double                               [RS] */
/*-------------------------------------------------------------------*/
DEF_INST(shift_left_double)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
U32     n;                              /* 32-bit operand values     */
U64     dreg;                           /* Double register work area */
U32     h, i, j, m;                     /* Integer work areas        */

    RS(inst, regs, r1, r3, b2, effective_addr2);

    ODD_CHECK(r1, regs);

    /* Use rightmost six bits of operand address as shift count */
    n = effective_addr2 & 0x3F;

    /* Load the signed value from the R1 and R1+1 registers */
    dreg = (U64)regs->GR_L(r1) << 32 | regs->GR_L(r1+1);
    m = ((S64)dreg < 0) ? 1 : 0;

    /* Shift the numeric portion of the value */
    for (i = 0, j = 0; i < n; i++)
    {
        /* Shift bits 1-63 left one bit position */
        dreg <<= 1;

        /* Overflow if bit shifted out is unlike the sign bit */
        h = ((S64)dreg < 0) ? 1 : 0;
        if (h != m)
            j = 1;
    }

    /* Load updated value into the R1 and R1+1 registers */
    regs->GR_L(r1) = (dreg >> 32) & 0x7FFFFFFF;
    if (m)
        regs->GR_L(r1) |= 0x80000000;
    regs->GR_L(r1+1) = dreg & 0xFFFFFFFF;

    /* Condition code 3 and program check if overflow occurred */
    if (j)
    {
        regs->psw.cc = 3;
        if ( FOMASK(&regs->psw) )
            ARCH_DEP(program_interrupt) (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
        return;
    }

    /* Set the condition code */
    regs->psw.cc = (S64)dreg > 0 ? 2 : (S64)dreg < 0 ? 1 : 0;

}


/*-------------------------------------------------------------------*/
/* 8D   SLDL  - Shift Left Double Logical                       [RS] */
/*-------------------------------------------------------------------*/
DEF_INST(shift_left_double_logical)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
U32     n;                              /* 32-bit operand values     */
U64     dreg;                           /* Double register work area */

    RS(inst, regs, r1, r3, b2, effective_addr2);

    ODD_CHECK(r1, regs);

    /* Use rightmost six bits of operand address as shift count */
    n = effective_addr2 & 0x3F;

    /* Shift the R1 and R1+1 registers */
    dreg = (U64)regs->GR_L(r1) << 32 | regs->GR_L(r1+1);
    dreg <<= n;
    regs->GR_L(r1) = dreg >> 32;
    regs->GR_L(r1+1) = dreg & 0xFFFFFFFF;

}


/*-------------------------------------------------------------------*/
/* 8B   SLA   - Shift Left Single                               [RS] */
/*-------------------------------------------------------------------*/
DEF_INST(shift_left_single)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
U32     n, n1, n2;                      /* 32-bit operand values     */
U32     i, j;                           /* Integer work areas        */

    RS(inst, regs, r1, r3, b2, effective_addr2);

    /* Use rightmost six bits of operand address as shift count */
    n = effective_addr2 & 0x3F;

    /* Fast path if no possible overflow */
    if (regs->GR_L(r1) < 0x10000 && n < 16)
    {
        regs->GR_L(r1) <<= n;
        regs->psw.cc = regs->GR_L(r1) ? 2 : 0;
        return;
    }

    /* Load the numeric and sign portions from the R1 register */
    n1 = regs->GR_L(r1) & 0x7FFFFFFF;
    n2 = regs->GR_L(r1) & 0x80000000;

    /* Shift the numeric portion left n positions */
    for (i = 0, j = 0; i < n; i++)
    {
        /* Shift bits 1-31 left one bit position */
        n1 <<= 1;

        /* Overflow if bit shifted out is unlike the sign bit */
        if ((n1 & 0x80000000) != n2)
            j = 1;
    }

    /* Load the updated value into the R1 register */
    regs->GR_L(r1) = (n1 & 0x7FFFFFFF) | n2;

    /* Condition code 3 and program check if overflow occurred */
    if (j)
    {
        regs->psw.cc = 3;
        if ( FOMASK(&regs->psw) )
            ARCH_DEP(program_interrupt) (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
        return;
    }

    /* Set the condition code */
    regs->psw.cc = (S32)regs->GR_L(r1) > 0 ? 2 :
                   (S32)regs->GR_L(r1) < 0 ? 1 : 0;

}


/*-------------------------------------------------------------------*/
/* 89   SLL   - Shift Left Single Logical                       [RS] */
/*-------------------------------------------------------------------*/
DEF_INST(shift_left_single_logical)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
U32     n;                              /* Integer work areas        */

    RS(inst, regs, r1, r3, b2, effective_addr2);

    /* Use rightmost six bits of operand address as shift count */
    n = effective_addr2 & 0x3F;

    /* Shift the R1 register */
    regs->GR_L(r1) = n > 31 ? 0 : regs->GR_L(r1) << n;
}


/*-------------------------------------------------------------------*/
/* 8E   SRDA  - Shift Right Double                              [RS] */
/*-------------------------------------------------------------------*/
DEF_INST(shift_right_double)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
U32     n;                              /* 32-bit operand values     */
U64     dreg;                           /* Double register work area */

    RS(inst, regs, r1, r3, b2, effective_addr2);

    ODD_CHECK(r1, regs);

    /* Use rightmost six bits of operand address as shift count */
    n = effective_addr2 & 0x3F;

    /* Shift the R1 and R1+1 registers */
    dreg = (U64)regs->GR_L(r1) << 32 | regs->GR_L(r1+1);
    dreg = (U64)((S64)dreg >> n);
    regs->GR_L(r1) = dreg >> 32;
    regs->GR_L(r1+1) = dreg & 0xFFFFFFFF;

    /* Set the condition code */
    regs->psw.cc = (S64)dreg > 0 ? 2 : (S64)dreg < 0 ? 1 : 0;

}


/*-------------------------------------------------------------------*/
/* 8C   SRDL  - Shift Right Double Logical                      [RS] */
/*-------------------------------------------------------------------*/
DEF_INST(shift_right_double_logical)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
U32     n;                              /* 32-bit operand values     */
U64     dreg;                           /* Double register work area */

    RS(inst, regs, r1, r3, b2, effective_addr2);

    ODD_CHECK(r1, regs);

        /* Use rightmost six bits of operand address as shift count */
    n = effective_addr2 & 0x3F;

    /* Shift the R1 and R1+1 registers */
    dreg = (U64)regs->GR_L(r1) << 32 | regs->GR_L(r1+1);
    dreg >>= n;
    regs->GR_L(r1) = dreg >> 32;
    regs->GR_L(r1+1) = dreg & 0xFFFFFFFF;

}


/*-------------------------------------------------------------------*/
/* 8A   SRA   - Shift Right single                              [RS] */
/*-------------------------------------------------------------------*/
DEF_INST(shift_right_single)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
U32     n;                              /* Integer work areas        */

    RS(inst, regs, r1, r3, b2, effective_addr2);

    /* Use rightmost six bits of operand address as shift count */
    n = effective_addr2 & 0x3F;

    /* Shift the signed value of the R1 register */
    regs->GR_L(r1) = n > 30 ?
                    ((S32)regs->GR_L(r1) < 0 ? -1 : 0) :
                    (S32)regs->GR_L(r1) >> n;

    /* Set the condition code */
    regs->psw.cc = ((S32)regs->GR_L(r1) > 0) ? 2 :
                   (((S32)regs->GR_L(r1) < 0) ? 1 : 0);
}


/*-------------------------------------------------------------------*/
/* 88   SRL   - Shift Right Single Logical                      [RS] */
/*-------------------------------------------------------------------*/
DEF_INST(shift_right_single_logical)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
U32     n;                              /* Integer work areas        */

    RS(inst, regs, r1, r3, b2, effective_addr2);

    /* Use rightmost six bits of operand address as shift count */
    n = effective_addr2 & 0x3F;

    /* Shift the R1 register */
    regs->GR_L(r1) = n > 31 ? 0 : regs->GR_L(r1) >> n;
}


/*-------------------------------------------------------------------*/
/* 50   ST    - Store                                           [RX] */
/*-------------------------------------------------------------------*/
DEF_INST(store)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U32    *p;                              /* Mainstor pointer          */

    RX(inst, regs, r1, b2, effective_addr2);

    /* Store register contents at operand address */
    if ((effective_addr2 & 3) == 0)
    {
        p = (U32*)MADDR(effective_addr2, b2, regs, ACCTYPE_WRITE, regs->psw.pkey);
        STORE_FW (p, regs->GR_L(r1));
    }
    else
        ARCH_DEP(vstore4) ( regs->GR_L(r1), effective_addr2, b2, regs );

} /* end DEF_INST(store) */


#if defined(FEATURE_ACCESS_REGISTERS)
/*-------------------------------------------------------------------*/
/* 9B   STAM  - Store Access Multiple                           [RS] */
/*-------------------------------------------------------------------*/
DEF_INST(store_access_multiple)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
int     i, m, n;                        /* Integer work area         */
U32    *p1, *p2 = NULL;                 /* Mainstor pointers         */

    RS(inst, regs, r1, r3, b2, effective_addr2);

    FW_CHECK(effective_addr2, regs);

    /* Calculate number of regs to store */
    n = ((r3 - r1) & 0xF) + 1;

    /* Calculate number of words to next boundary */
    m = (0x800 - (effective_addr2 & 0x7ff)) >> 2;

    /* Address of operand beginning */
    p1 = (U32*)MADDR(effective_addr2, b2, regs, ACCTYPE_WRITE, regs->psw.pkey);

    /* Get address of next page if boundary crossed */
    if (unlikely (m < n))
        p2 = (U32*)MADDR(effective_addr2 + (m*4), b2, regs, ACCTYPE_WRITE, regs->psw.pkey);
    else
        m = n;

    /* Store to first page */
    for (i = 0; i < m; i++)
        store_fw (p1++, regs->AR((r1 + i) & 0xF));

    /* Store to next page */
    for ( ; i < n; i++)
        store_fw (p2++, regs->AR((r1 + i) & 0xF));

}
#endif /*defined(FEATURE_ACCESS_REGISTERS)*/


/*-------------------------------------------------------------------*/
/* 42   STC   - Store Character                                 [RX] */
/*-------------------------------------------------------------------*/
DEF_INST(store_character)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */

    RX(inst, regs, r1, b2, effective_addr2);

    /* Store rightmost byte of R1 register at operand address */
    ARCH_DEP(vstoreb) ( regs->GR_LHLCL(r1), effective_addr2, b2, regs );
}


/*-------------------------------------------------------------------*/
/* BE   STCM  - Store Characters under Mask                     [RS] */
/*-------------------------------------------------------------------*/
DEF_INST(store_characters_under_mask)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
int     i;                              /* Integer work area         */
BYTE    rbyte[4];                       /* Byte work area            */

    RS(inst, regs, r1, r3, b2, effective_addr2);

    switch (r3) {

    case 7:
        /* Optimized case */
        store_fw(rbyte, regs->GR_L(r1));
        ARCH_DEP(vstorec) (rbyte+1, 2, effective_addr2, b2, regs);
        break;

    case 15:
        /* Optimized case */
        ARCH_DEP(vstore4) (regs->GR_L(r1), effective_addr2, b2, regs);
        break;

    default:
        /* Extract value from register by mask */
        i = 0;
        if (r3 & 0x8) rbyte[i++] = (regs->GR_L(r1) >> 24) & 0xFF;
        if (r3 & 0x4) rbyte[i++] = (regs->GR_L(r1) >> 16) & 0xFF;
        if (r3 & 0x2) rbyte[i++] = (regs->GR_L(r1) >>  8) & 0xFF;
        if (r3 & 0x1) rbyte[i++] = (regs->GR_L(r1)      ) & 0xFF;

        if (i)
            ARCH_DEP(vstorec) (rbyte, i-1, effective_addr2, b2, regs);
#if defined(MODEL_DEPENDENT_STCM)
        /* If the mask is all zero, we nevertheless access one byte
           from the storage operand, because POP states that an
           access exception may be recognized on the first byte */
        else
            ARCH_DEP(validate_operand) (effective_addr2, b2, 0,
                                        ACCTYPE_WRITE, regs);
#endif
        break;

    } /* switch (r3) */
}


/*-------------------------------------------------------------------*/
/* B205 STCK  - Store Clock                                      [S] */
/*-------------------------------------------------------------------*/
DEF_INST(store_clock)
{
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U64     dreg;                           /* Double word work area     */

    S(inst, regs, b2, effective_addr2);

#if defined(_FEATURE_SIE)
    if(SIE_STATB(regs, IC2, STCK))
        longjmp(regs->progjmp, SIE_INTERCEPT_INST);
#endif /*defined(_FEATURE_SIE)*/

    /* Perform serialization before fetching clock */
    PERFORM_SERIALIZATION (regs);

    /* Obtain the TOD clock update lock */
    obtain_lock (&sysblk.todlock);

    /* Update the TOD clock value */
    update_TOD_clock();

    /* Retrieve the TOD clock value and shift out the epoch */
    dreg = ((sysblk.todclk + regs->todoffset) << 8) | regs->cpuad;

    /* Release the TOD clock update lock */
    release_lock (&sysblk.todlock);

// /*debug*/logmsg("Store TOD clock=%16.16llX\n", dreg);

    /* Store TOD clock value at operand address */
    ARCH_DEP(vstore8) ( dreg, effective_addr2, b2, regs );

    /* Perform serialization after storing clock */
    PERFORM_SERIALIZATION (regs);

    /* Set condition code zero */
    regs->psw.cc = 0;

}


#if defined(FEATURE_EXTENDED_TOD_CLOCK)
/*-------------------------------------------------------------------*/
/* B278 STCKE - Store Clock Extended                             [S] */
/*-------------------------------------------------------------------*/
DEF_INST(store_clock_extended)
{
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U64     dreg;                           /* Double word work area     */

    S(inst, regs, b2, effective_addr2);

#if defined(_FEATURE_SIE)
    if(SIE_STATB(regs, IC2, STCK))
        longjmp(regs->progjmp, SIE_INTERCEPT_INST);
#endif /*defined(_FEATURE_SIE)*/

    /* Perform serialization before fetching clock */
    PERFORM_SERIALIZATION (regs);

    /* Obtain the TOD clock update lock */
    obtain_lock (&sysblk.todlock);

    /* Update the TOD clock value */
    update_TOD_clock();

    /* Retrieve the TOD epoch, clock bits 0-51, and 4 zeroes */
    dreg = (sysblk.todclk + regs->todoffset);

    /* Release the TOD clock update lock */
    release_lock (&sysblk.todlock);

    /* Check that all 16 bytes of the operand are accessible */
    ARCH_DEP(validate_operand) (effective_addr2, b2, 15, ACCTYPE_WRITE, regs);

//  /*debug*/logmsg("Store TOD clock extended: +0=%16.16llX\n",
//  /*debug*/       dreg);

    /* Store the 8 bit TOD epoch, clock bits 0-51, and bits
       20-23 of the TOD uniqueness value at operand address */
    ARCH_DEP(vstore8) ( dreg, effective_addr2, b2, regs );

//  /*debug*/logmsg("Store TOD clock extended: +8=%16.16llX\n",
//  /*debug*/       dreg);

    /* Store second doubleword value at operand+8 */
    effective_addr2 += 8;
    effective_addr2 &= ADDRESS_MAXWRAP(regs);

    /* Store nonzero value in pos 72 to 111 */
    dreg = (dreg << 21) | 0x00100000 | (regs->cpuad << 16) | regs->todpr;

    ARCH_DEP(vstore8) ( dreg, effective_addr2, b2, regs );

    /* Perform serialization after storing clock */
    PERFORM_SERIALIZATION (regs);

    /* Set condition code zero */
    regs->psw.cc = 0;
}
#endif /*defined(FEATURE_EXTENDED_TOD_CLOCK)*/


/*-------------------------------------------------------------------*/
/* 40   STH   - Store Halfword                                  [RX] */
/*-------------------------------------------------------------------*/
DEF_INST(store_halfword)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */

    RX(inst, regs, r1, b2, effective_addr2);

    /* Store rightmost 2 bytes of R1 register at operand address */
    ARCH_DEP(vstore2) ( regs->GR_LHL(r1), effective_addr2, b2, regs );
}


/*-------------------------------------------------------------------*/
/* 90   STM   - Store Multiple                                  [RS] */
/*-------------------------------------------------------------------*/
DEF_INST(store_multiple)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
int     i, m, n, w = 0;                 /* Integer work area         */
U32    *p1, *p2 = NULL;                 /* Mainstor pointers         */
U32     rwork[16];                      /* Intermediate work area    */

    RS(inst, regs, r1, r3, b2, effective_addr2);

    /* Calculate number of regs to store */
    n = ((r3 - r1) & 0xF) + 1;

    /* Calculate number of words to next boundary */
    m = (0x800 - (effective_addr2 & 0x7ff)) >> 2;
    if (unlikely((effective_addr2 & 3) && m < n))
    {
        m = n;
        p1 = rwork;
        w = 1;
    }
    else
    {
        /* Address of operand beginning */
        p1 = (U32*)MADDR(effective_addr2, b2, regs, ACCTYPE_WRITE, regs->psw.pkey);

        /* Get address of next page if boundary crossed */
        if (unlikely (m < n))
            p2 = (U32*)MADDR(effective_addr2 + (m*4), b2, regs, ACCTYPE_WRITE, regs->psw.pkey);
        else
            m = n;
    }

    /* Store to first page */
    for (i = 0; i < m; i++)
        store_fw (p1++, regs->GR_L((r1 + i) & 0xF));

    /* Store to next page */
    for ( ; i < n; i++)
        store_fw (p2++, regs->GR_L((r1 + i) & 0xF));

    if (unlikely(w))
        ARCH_DEP(vstorec) (rwork, (n * 4) - 1, effective_addr2, b2, regs);

} /* end DEF_INST(store_multiple) */


/*-------------------------------------------------------------------*/
/* 1B   SR    - Subtract Register                               [RR] */
/*-------------------------------------------------------------------*/
DEF_INST(subtract_register)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, regs, r1, r2);

    /* Subtract signed operands and set condition code */
    regs->psw.cc =
            sub_signed (&(regs->GR_L(r1)),
                    regs->GR_L(r1),
                    regs->GR_L(r2));

    /* Program check if fixed-point overflow */
    if ( regs->psw.cc == 3 && FOMASK(&regs->psw) )
        ARCH_DEP(program_interrupt) (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
}


/*-------------------------------------------------------------------*/
/* 5B   S     - Subtract                                        [RX] */
/*-------------------------------------------------------------------*/
DEF_INST(subtract)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */

    RX(inst, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = ARCH_DEP(vfetch4) ( effective_addr2, b2, regs );

    /* Subtract signed operands and set condition code */
    regs->psw.cc =
            sub_signed (&(regs->GR_L(r1)),
                    regs->GR_L(r1),
                    n);

    /* Program check if fixed-point overflow */
    if ( regs->psw.cc == 3 && FOMASK(&regs->psw) )
        ARCH_DEP(program_interrupt) (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
}


/*-------------------------------------------------------------------*/
/* 4B   SH    - Subtract Halfword                               [RX] */
/*-------------------------------------------------------------------*/
DEF_INST(subtract_halfword)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */

    RX(inst, regs, r1, b2, effective_addr2);

    /* Load 2 bytes from operand address */
    n = (S16)ARCH_DEP(vfetch2) ( effective_addr2, b2, regs );

    /* Subtract signed operands and set condition code */
    regs->psw.cc =
            sub_signed (&(regs->GR_L(r1)),
                    regs->GR_L(r1),
                    n);

    /* Program check if fixed-point overflow */
    if ( regs->psw.cc == 3 && FOMASK(&regs->psw) )
        ARCH_DEP(program_interrupt) (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
}


/*-------------------------------------------------------------------*/
/* 1F   SLR   - Subtract Logical Register                       [RR] */
/*-------------------------------------------------------------------*/
DEF_INST(subtract_logical_register)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, regs, r1, r2);

    /* Subtract unsigned operands and set condition code */
    if (likely(r1 == r2))
    {
        regs->psw.cc = 2;
        regs->GR_L(r1) = 0;
    }
    else
        regs->psw.cc =
            sub_logical (&(regs->GR_L(r1)),
                           regs->GR_L(r1),
                           regs->GR_L(r2));
}


/*-------------------------------------------------------------------*/
/* 5F   SL    - Subtract Logical                                [RX] */
/*-------------------------------------------------------------------*/
DEF_INST(subtract_logical)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */

    RX(inst, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = ARCH_DEP(vfetch4) ( effective_addr2, b2, regs );

    /* Subtract unsigned operands and set condition code */
    regs->psw.cc =
            sub_logical (&(regs->GR_L(r1)),
                    regs->GR_L(r1),
                    n);
}


/*-------------------------------------------------------------------*/
/* 0A   SVC   - Supervisor Call                                 [RR] */
/*-------------------------------------------------------------------*/
DEF_INST(supervisor_call)
{
BYTE    i;                              /* Instruction byte 1        */
PSA    *psa;                            /* -> prefixed storage area  */
RADR    px;                             /* prefix                    */
int     rc;                             /* Return code               */

    RR_SVC(inst, regs, i);
#if defined(FEATURE_ECPSVM)
    if(ecpsvm_dosvc(regs,i)==0)
    {
        return;
    }
#endif

#if defined(_FEATURE_SIE)
    if(SIE_MODE(regs) &&
      ( (regs->siebk->svc_ctl[0] & SIE_SVC0_ALL)
        || ( (regs->siebk->svc_ctl[0] & SIE_SVC0_1N) &&
              regs->siebk->svc_ctl[1] == i)
        || ( (regs->siebk->svc_ctl[0] & SIE_SVC0_2N) &&
              regs->siebk->svc_ctl[2] == i)
        || ( (regs->siebk->svc_ctl[0] & SIE_SVC0_3N) &&
              regs->siebk->svc_ctl[3] == i) ))
        longjmp(regs->progjmp, SIE_INTERCEPT_INST);
#endif /*defined(_FEATURE_SIE)*/

    px = regs->PX;
    SIE_TRANSLATE(&px, ACCTYPE_WRITE, regs);

    /* Set the main storage reference and change bits */
    STORAGE_KEY(px, regs) |= (STORKEY_REF | STORKEY_CHANGE);

    /* Use the I-byte to set the SVC interruption code */
    regs->psw.intcode = i;

/* OS/390 2.10 debugging for negative length GETMAIN */
#if 0
    if ( (i == 0x78) || (i == 0x0a) )
    {
        ARCH_DEP(display_inst) (regs, regs->ip);
    }
#endif

    /* Point to PSA in main storage */
    psa = (void*)(regs->mainstor + px);

#if defined(FEATURE_BCMODE)
    /* For ECMODE, store SVC interrupt code at PSA+X'88' */
    if ( ECMODE(&regs->psw) )
#endif /*defined(FEATURE_BCMODE)*/
    {
        psa->svcint[0] = 0;
        psa->svcint[1] = REAL_ILC(regs);
        psa->svcint[2] = 0;
        psa->svcint[3] = i;
    }

    /* Store current PSW at PSA+X'20' */
    ARCH_DEP(store_psw) ( regs, psa->svcold );

    /* Load new PSW from PSA+X'60' */
    if ( (rc = ARCH_DEP(load_psw) ( regs, psa->svcnew ) ) )
    {
        regs->psw.zeroilc = 0;
        ARCH_DEP(program_interrupt) (regs, rc);
    }

    /* Perform serialization and checkpoint synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);

    RETURN_INTCHECK(regs);

}


/*-------------------------------------------------------------------*/
/* 93   TS    - Test and Set                                     [S] */
/*-------------------------------------------------------------------*/
DEF_INST(test_and_set)
{
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
BYTE   *main2;                          /* Mainstor address          */
BYTE    old;                            /* Old value                 */

    S(inst, regs, b2, effective_addr2);

    /* Perform serialization before starting operation */
    PERFORM_SERIALIZATION (regs);

    /* Get operand absolute address */
    main2 = MADDR (effective_addr2, b2, regs, ACCTYPE_WRITE, regs->psw.pkey);

    /* Obtain main-storage access lock */
    OBTAIN_MAINLOCK(regs);

    /* Get old value */
    old = *main2;

    /* Attempt to exchange the values */
    while (cmpxchg1(&old, 255, main2));
    regs->psw.cc = old >> 7;

    /* Release main-storage access lock */
    RELEASE_MAINLOCK(regs);

    /* Perform serialization after completing operation */
    PERFORM_SERIALIZATION (regs);

    if (regs->psw.cc == 1)
    {
#if defined(_FEATURE_SIE)
        if(SIE_STATB(regs, IC0, TS1))
        {
            if( !OPEN_IC_PERINT(regs) )
                longjmp(regs->progjmp, SIE_INTERCEPT_INST);
            else
                longjmp(regs->progjmp, SIE_INTERCEPT_INSTCOMP);
        }
        else
#endif /*defined(_FEATURE_SIE)*/
            if (sysblk.cpus > 1)
                sched_yield();
    }
}


/*-------------------------------------------------------------------*/
/* 91   TM    - Test under Mask                                 [SI] */
/*-------------------------------------------------------------------*/
DEF_INST(test_under_mask)
{
BYTE    i2;                             /* Immediate operand         */
int     b1;                             /* Base of effective addr    */
VADR    effective_addr1;                /* Effective address         */
BYTE    tbyte;                          /* Work byte                 */

    SI(inst, regs, i2, b1, effective_addr1);

    /* Fetch byte from operand address */
    tbyte = ARCH_DEP(vfetchb) ( effective_addr1, b1, regs );

    /* AND with immediate operand mask */
    tbyte &= i2;

    /* Set condition code according to result */
    regs->psw.cc =
            ( tbyte == 0 ) ? 0 :            /* result all zeroes */
            ((tbyte^i2) == 0) ? 3 :         /* result all ones   */
            1 ;                             /* result mixed      */
}


#if defined(FEATURE_IMMEDIATE_AND_RELATIVE)
/*-------------------------------------------------------------------*/
/* A7x0 TMH   - Test under Mask High                            [RI] */
/*-------------------------------------------------------------------*/
DEF_INST(test_under_mask_high)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */
U16     h1;                             /* 16-bit operand values     */
U16     h2;                             /* 16-bit operand values     */

    RI(inst, regs, r1, opcd, i2);

    /* AND register bits 0-15 with immediate operand */
    h1 = i2 & regs->GR_LHH(r1);

    /* Isolate leftmost bit of immediate operand */
    for ( h2 = 0x8000; h2 != 0 && (h2 & i2) == 0; h2 >>= 1 );

    /* Set condition code according to result */
    regs->psw.cc =
            ( h1 == 0 ) ? 0 :           /* result all zeroes */
            ((h1 ^ i2) == 0) ? 3 :      /* result all ones   */
            ((h1 & h2) == 0) ? 1 :      /* leftmost bit zero */
            2;                          /* leftmost bit one  */
}
#endif /*defined(FEATURE_IMMEDIATE_AND_RELATIVE)*/


#if defined(FEATURE_IMMEDIATE_AND_RELATIVE)
/*-------------------------------------------------------------------*/
/* A7x1 TML   - Test under Mask Low                             [RI] */
/*-------------------------------------------------------------------*/
DEF_INST(test_under_mask_low)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */
U16     h1;                             /* 16-bit operand values     */
U16     h2;                             /* 16-bit operand values     */

    RI(inst, regs, r1, opcd, i2);

    /* AND register bits 16-31 with immediate operand */
    h1 = i2 & regs->GR_LHL(r1);

    /* Isolate leftmost bit of immediate operand */
    for ( h2 = 0x8000; h2 != 0 && (h2 & i2) == 0; h2 >>= 1 );

    /* Set condition code according to result */
    regs->psw.cc =
            ( h1 == 0 ) ? 0 :           /* result all zeroes */
            ((h1 ^ i2) == 0) ? 3 :      /* result all ones   */
            ((h1 & h2) == 0) ? 1 :      /* leftmost bit zero */
            2;                          /* leftmost bit one  */

}
#endif /*defined(FEATURE_IMMEDIATE_AND_RELATIVE)*/


/*-------------------------------------------------------------------*/
/* DC   TR    - Translate                                       [SS] */
/*-------------------------------------------------------------------*/
DEF_INST(translate)
{
int     l;                              /* Lenght byte               */
int     b1, b2;                         /* Values of base field      */
VADR    effective_addr1,
        effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */
BYTE    sbyte;                          /* Byte work areas           */
int     d;                              /* Integer work areas        */
int     h;                              /* Integer work areas        */
int     i;                              /* Integer work areas        */
BYTE    cwork[256];                     /* Character work areas      */

    SS_L(inst, regs, l, b1, effective_addr1,
                                  b2, effective_addr2);

    /* If operand 1 crosses a page, make sure both pages are accessable */
    if((effective_addr1 & PAGEFRAME_PAGEMASK) !=
        ((effective_addr1 + l) & PAGEFRAME_PAGEMASK))
        ARCH_DEP(validate_operand) (effective_addr1, b1, l, ACCTYPE_WRITE_SKP, regs);

    /* Fetch first operand into work area */
    ARCH_DEP(vfetchc) ( cwork, l, effective_addr1, b1, regs );

    /* Determine the second operand range by scanning the
       first operand to find the bytes with the highest
       and lowest values */
    for ( i = 0, d = 255, h = 0; i <= l; i++ )
    {
        if (cwork[i] < d) d = cwork[i];
        if (cwork[i] > h) h = cwork[i];
    }

    n = (effective_addr2 + d) & ADDRESS_MAXWRAP(regs);
    /* If operand 2 crosses a page, make sure both pages are accessable */
    if((n & PAGEFRAME_PAGEMASK) !=
        ((n + (h-d)) & PAGEFRAME_PAGEMASK))
        ARCH_DEP(validate_operand) (n, b2, h-d, ACCTYPE_READ, regs);

    /* Process first operand from left to right, refetching
       second operand and storing the result byte by byte
       to ensure correct handling of overlapping operands */
    for ( i = 0; i <= l; i++ )
    {
        /* Fetch byte from second operand */
        n = (effective_addr2 + cwork[i]) & ADDRESS_MAXWRAP(regs);
        sbyte = ARCH_DEP(vfetchb) ( n, b2, regs );

        /* Store result at first operand address */
        ARCH_DEP(vstoreb) ( sbyte, effective_addr1, b1, regs );

        /* Increment first operand address */
        effective_addr1++;
        effective_addr1 &= ADDRESS_MAXWRAP(regs);

    } /* end for(i) */
}


/*-------------------------------------------------------------------*/
/* DD   TRT   - Translate and Test                              [SS] */
/*-------------------------------------------------------------------*/
DEF_INST(translate_and_test)
{
int     l;                              /* Lenght byte               */
int     b1, b2;                         /* Values of base field      */
VADR    effective_addr1,
        effective_addr2;                /* Effective addresses       */
int     cc = 0;                         /* Condition code            */
BYTE    sbyte;                          /* Byte work areas           */
BYTE    dbyte;                          /* Byte work areas           */
int     i;                              /* Integer work areas        */

    SS_L(inst, regs, l, b1, effective_addr1,
                                  b2, effective_addr2);

    /* Process first operand from left to right */
    for ( i = 0; i <= l; i++ )
    {
        /* Fetch argument byte from first operand */
        dbyte = ARCH_DEP(vfetchb) ( effective_addr1, b1, regs );

        /* Fetch function byte from second operand */
        sbyte = ARCH_DEP(vfetchb) ( (effective_addr2 + dbyte)
                                   & ADDRESS_MAXWRAP(regs), b2, regs );

        /* Test for non-zero function byte */
        if (sbyte != 0) {

            /* Store address of argument byte in register 1 */
#if defined(FEATURE_ESAME)
            if(regs->psw.amode64)
                regs->GR_G(1) = effective_addr1;
            else
#endif
            if ( regs->psw.amode )
                regs->GR_L(1) = effective_addr1;
            else
                regs->GR_LA24(1) = effective_addr1;

            /* Store function byte in low-order byte of reg.2 */
            regs->GR_LHLCL(2) = sbyte;

            /* Set condition code 2 if argument byte was last byte
               of first operand, otherwise set condition code 1 */
            cc = (i == l) ? 2 : 1;

            /* Terminate the operation at this point */
            break;

        } /* end if(sbyte) */

        /* Increment first operand address */
        effective_addr1++;
        effective_addr1 &= ADDRESS_MAXWRAP(regs);

    } /* end for(i) */

    /* Update the condition code */
    regs->psw.cc = cc;
}


#ifdef FEATURE_EXTENDED_TRANSLATION
/*-------------------------------------------------------------------*/
/* B2A5 TRE   - Translate Extended                             [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(translate_extended)
{
int     r1, r2;                         /* Values of R fields        */
int     i;                              /* Loop counter              */
int     cc = 0;                         /* Condition code            */
VADR    addr1, addr2;                   /* Operand addresses         */
GREG    len1;                           /* Operand length            */
BYTE    byte1, byte2;                   /* Operand bytes             */
BYTE    tbyte;                          /* Test byte                 */
BYTE    trtab[256];                     /* Translate table           */

    RRE(inst, regs, r1, r2);

    ODD_CHECK(r1, regs);

    /* Load the test byte from bits 24-31 of register 0 */

    tbyte = regs->GR_LHLCL(0);

    /* Load the operand addresses */
    addr1 = regs->GR(r1) & ADDRESS_MAXWRAP(regs);
    addr2 = regs->GR(r2) & ADDRESS_MAXWRAP(regs);

    /* Load first operand length from R1+1 */
    len1 = GR_A(r1+1, regs);

    /* Fetch second operand into work area.
       [7.5.101] Access exceptions for all 256 bytes of the second
       operand may be recognized, even if not all bytes are used */
    ARCH_DEP(vfetchc) ( trtab, 255, addr2, r2, regs );

    /* Process first operand from left to right */
    for (i = 0; len1 > 0; i++)
    {
        /* If 4096 bytes have been compared, exit with condition code 3 */
        if (i >= 4096)
        {
            cc = 3;
            break;
        }

        /* Fetch byte from first operand */
        byte1 = ARCH_DEP(vfetchb) ( addr1, r1, regs );

        /* If equal to test byte, exit with condition code 1 */
        if (byte1 == tbyte)
        {
            cc = 1;
            break;
        }

        /* Load indicated byte from translate table */
        byte2 = trtab[byte1];

        /* Store result at first operand address */
        ARCH_DEP(vstoreb) ( byte2, addr1, r1, regs );
        addr1++;
        addr1 &= ADDRESS_MAXWRAP(regs);
        len1--;

        /* Update the registers */
        SET_GR_A(r1, regs, addr1);
        SET_GR_A(r1+1, regs, len1);

    } /* end for(i) */

    /* Set condition code */
    regs->psw.cc =  cc;

} /* end translate_extended */
#endif /*FEATURE_EXTENDED_TRANSLATION*/


/*-------------------------------------------------------------------*/
/* F3   UNPK  - Unpack                                          [SS] */
/*-------------------------------------------------------------------*/
DEF_INST(unpack)
{
int     l1, l2;                         /* Register numbers          */
int     b1, b2;                         /* Base registers            */
VADR    effective_addr1,
        effective_addr2;                /* Effective addressES       */
int     i, j;                           /* Loop counters             */
BYTE    sbyte;                          /* Source operand byte       */
BYTE    rbyte;                          /* Right result byte of pair */
BYTE    lbyte;                          /* Left result byte of pair  */

    SS(inst, regs, l1, l2, b1, effective_addr1,
                                     b2, effective_addr2);

    /* If operand 1 crosses a page, make sure both pages are accessable */
    if((effective_addr1 & PAGEFRAME_PAGEMASK) !=
        ((effective_addr1 + l1) & PAGEFRAME_PAGEMASK))
        ARCH_DEP(validate_operand) (effective_addr1, b1, l1, ACCTYPE_WRITE_SKP, regs);

    /* If operand 2 crosses a page, make sure both pages are accessable */
    if((effective_addr2 & PAGEFRAME_PAGEMASK) !=
        ((effective_addr2 + l2) & PAGEFRAME_PAGEMASK))
        ARCH_DEP(validate_operand) (effective_addr2, b2, l2, ACCTYPE_READ, regs);

    /* Exchange the digits in the rightmost byte */
    effective_addr1 += l1;
    effective_addr2 += l2;
    sbyte = ARCH_DEP(vfetchb) ( effective_addr2, b2, regs );
    rbyte = (sbyte << 4) | (sbyte >> 4);
    ARCH_DEP(vstoreb) ( rbyte, effective_addr1, b1, regs );

    /* Process remaining bytes from right to left */
    for (i = l1, j = l2; i > 0; i--)
    {
        /* Fetch source byte from second operand */
        if (j-- > 0)
        {
            sbyte = ARCH_DEP(vfetchb) ( --effective_addr2, b2, regs );
            rbyte = (sbyte & 0x0F) | 0xF0;
            lbyte = (sbyte >> 4) | 0xF0;
        }
        else
        {
            rbyte = 0xF0;
            lbyte = 0xF0;
        }

        /* Store unpacked bytes at first operand address */
        ARCH_DEP(vstoreb) ( rbyte, --effective_addr1, b1, regs );
        if (--i > 0)
        {
            effective_addr1 &= ADDRESS_MAXWRAP(regs);
            ARCH_DEP(vstoreb) ( lbyte, --effective_addr1, b1, regs );
        }

        /* Wraparound according to addressing mode */
        effective_addr1 &= ADDRESS_MAXWRAP(regs);
        effective_addr2 &= ADDRESS_MAXWRAP(regs);

    } /* end for(i) */

}


/*-------------------------------------------------------------------*/
/* 0102 UPT   - Update Tree                                      [E] */
/*              (c) Copyright Peter Kuschnerus, 1999-2005            */
/*              (c) Copyright "Fish" (David B. Trout), 2005          */
/*-------------------------------------------------------------------*/

DEF_INST(update_tree)
{
GREG    index;                          /* tree index                */
GREG    nodecode;                       /* current node's codeword   */
GREG    nodedata;                       /* current node's other data */
VADR    nodeaddr;                       /* work addr of current node */
#if defined(FEATURE_ESAME)
BYTE    a64 = regs->psw.amode64;        /* 64-bit mode flag          */
#endif

    E(inst, regs);

    UNREFERENCED(inst);

    /*
    **  GR0, GR1    node values (codeword and other data) of node
    **              with "highest encountered codeword value"
    **  GR2, GR3    node values (codeword and other data) from whichever
    **              node we happened to have encountered that had a code-
    **              word value equal to our current "highest encountered
    **              codeword value" (e.g. GR0)  (cc0 only)
    **  GR4         pointer to one node BEFORE the beginning of the tree
    **  GR5         current node index (tree displacement to current node)
    */

    /* Check GR4, GR5 for proper alignment */
    if (0
        || ( GR_A(4,regs) & UPT_ALIGN_MASK )
        || ( GR_A(5,regs) & UPT_ALIGN_MASK )
    )
        ARCH_DEP(program_interrupt) (regs, PGM_SPECIFICATION_EXCEPTION);

    /* Bubble the tree by moving successively higher nodes towards the
       front (beginning) of the tree, only stopping whenever we either:

            1. reach the beginning of the tree, -OR-
            2. encounter a node with a negative codeword value, -OR-
            3. encounter a node whose codeword is equal to
               our current "highest encountered codeword".

       Thus, when we're done, GR0 & GR1 will then contain the node values
       of the node with the highest encountered codeword value, and all
       other traversed nodes will have been reordered into descending code-
       word sequence (i.e. from highest codeword value to lowest codeword
       value; this is after all an instruction used for sorting/merging).
    */

    for (;;)
    {
        /* Calculate index value of next node to be examined (half
           as far from beginning of tree to where we currently are)
        */
        index = (GR_A(5,regs) >> 1) & UPT_SHIFT_MASK;

        /* Exit with cc1 when we've gone as far as we can go */
        if ( !index )
        {
            regs->psw.cc = 1;
            break;
        }

        /* Exit with cc3 when we encounter a negative codeword value
           (i.e. any codeword value with its highest-order bit on)
        */
        if ( GR_A(0,regs) & UPT_HIGH_BIT )
        {
            regs->psw.cc = 3;
            break;
        }

        /* Retrieve this node's values for closer examination... */

        nodeaddr = regs->GR(4) + index;

#if defined(FEATURE_ESAME)
        if ( a64 )
        {
            nodecode = ARCH_DEP(vfetch8) ( (nodeaddr+0) & ADDRESS_MAXWRAP(regs), AR4, regs );
            nodedata = ARCH_DEP(vfetch8) ( (nodeaddr+8) & ADDRESS_MAXWRAP(regs), AR4, regs );
        }
        else
#endif
        {
            nodecode = ARCH_DEP(vfetch4) ( (nodeaddr+0) & ADDRESS_MAXWRAP(regs), AR4, regs );
            nodedata = ARCH_DEP(vfetch4) ( (nodeaddr+4) & ADDRESS_MAXWRAP(regs), AR4, regs );
        }

        /* GR5 must remain UNCHANGED if the execution of a unit of operation
           is nullified or suppressed! Thus it must ONLY be updated/committed
           AFTER we've successfully retrieved the node data (since the storage
           access could cause a program-check thereby nullifying/suppressing
           the instruction's "current unit of operation")
        */
        SET_GR_A(5,regs,index);     // (do AFTER node data is accessed!)

        /* Exit with cc0 whenever we reach a node whose codeword is equal
           to our current "highest encountered" codeword value (i.e. any
           node whose codeword matches our current "highest" (GR0) value)
        */
        if ( nodecode == GR_A(0,regs) )
        {
            /* Load GR2 and GR3 with the equal codeword node's values */
            SET_GR_A(2,regs,nodecode);
            SET_GR_A(3,regs,nodedata);
            regs->psw.cc = 0;
            return;
        }

        /* Keep resequencing the tree's nodes, moving successively higher
           nodes to the front (beginning of tree)...
        */
        if ( nodecode < GR_A(0,regs) )
            continue;

        /* This node has a codeword value higher than our currently saved
           highest encountered codeword value (GR0). Swap our GR0/1 values
           with this node's values, such that GR0/1 always hold the values
           from the node with the highest encountered codeword value...
        */

        /* Store obsolete GR0 and GR1 values into this node's entry */
#if defined(FEATURE_ESAME)
        if ( a64 )
        {
            ARCH_DEP(vstore8) ( GR_A(0,regs), (nodeaddr+0) & ADDRESS_MAXWRAP(regs), AR4, regs );
            ARCH_DEP(vstore8) ( GR_A(1,regs), (nodeaddr+8) & ADDRESS_MAXWRAP(regs), AR4, regs );
        }
        else
#endif
        {
            ARCH_DEP(vstore4) ( GR_A(0,regs), (nodeaddr+0) & ADDRESS_MAXWRAP(regs), AR4, regs );
            ARCH_DEP(vstore4) ( GR_A(1,regs), (nodeaddr+4) & ADDRESS_MAXWRAP(regs), AR4, regs );
        }

        /* Update GR0 and GR1 with the new "highest encountered" values */
        SET_GR_A(0,regs,nodecode);
        SET_GR_A(1,regs,nodedata);
    }

    /* Commit GR5 with the actual index value we stopped on */
    SET_GR_A(5,regs,index);
}

#if defined(FEATURE_EXTENDED_TRANSLATION_FACILITY_3)
/*-------------------------------------------------------------------*/
/* B9B0 CU14 - Convert UTF-8 to UTF-32                         [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(convert_utf8_to_utf32)
{
  VADR dest;                       /* Destination address            */
  GREG destlen;                    /* Destination length             */
  int r1;
  int r2;
  int read;                        /* Bytes read                     */
  VADR srce;                       /* Source address                 */
  GREG srcelen;                    /* Source length                  */
  BYTE utf32[4];                   /* utf32 character(s)             */
  BYTE utf8[4];                    /* utf8 character(s)              */
  int wfc;                         /* Well-Formedness-Checking (W)   */
  int xlated;                      /* characters translated          */

  RRE(inst, regs, r1, r2);
  ODD2_CHECK(r1, r2, regs);

  /* Get paramaters */
  dest = regs->GR(r1) & ADDRESS_MAXWRAP(regs);
  destlen = GR_A(r1 + 1, regs);
  srce = regs->GR(r2) & ADDRESS_MAXWRAP(regs);
  srcelen = GR_A(r2 + 1, regs);
  if(inst[1] & 0x10)
    wfc = 1;
  else
    wfc = 0;

  /* Every valid utf-32 starts with 0x00 */
  utf32[0] = 0x00;

  /* Initialize number of translated charachters */
  xlated = 0;
  while(xlated < 4096)
  {
    /* Check end of source or destination */
    if(srcelen < 1)
    {
      regs->psw.cc = 0;
      return;
    }
    if(destlen < 4)
    {
      regs->psw.cc = 1;
      return;
    }

    /* Fetch a byte */
    utf8[0] = ARCH_DEP(vfetchb)(srce, r2, regs);
    if(utf8[0] < 0x80)
    {
      /* xlate range 00-7f */
      /* 0jklmnop -> 00000000 00000000 00000000 0jklmnop */
      utf32[1] = 0x00;
      utf32[2] = 0x00;
      utf32[3] = utf8[0];
      read = 1;
    }
    else if(utf8[0] >= 0xc0 && utf8[0] <= 0xdf)
    {
      /* WellFormednessChecking */
      if(wfc)
      {
        if(utf8[0] <= 0xc1)
        {
          regs->psw.cc = 2;
          return;
        }
      }

      /* Check end of source */
      if(srcelen < 2)
      {
        regs->psw.cc = 0;   /* Strange but stated in POP */
        return;
      }

      /* Get the next byte */
      utf8[1] = ARCH_DEP(vfetchb)(srce + 1, r2, regs);

      /* WellFormednessChecking */
      if(wfc)
      {
        if(utf8[1] < 0x80 || utf8[1] > 0xbf)
        {
          regs->psw.cc = 2;
          return;
        }
      }

      /* xlate range c000-dfff */
      /* 110fghij 10klmnop -> 00000000 00000000 00000fgh ijklmnop */
      utf32[1] = 0x00;
      utf32[2] = (utf8[0] & 0x1c) >> 2;
      utf32[3] = (utf8[0] << 6) & (utf8[1] & 0x3f);
      read = 2;
    }
    else if(utf8[0] >= 0xe0 && utf8[0] <= 0xef)
    {
      /* Check end of source */
      if(srcelen < 3)
      {
        regs->psw.cc = 0;   /* Strange but stated in POP */
        return;
      }

      /* Get the next 2 bytes */
      ARCH_DEP(vfetchc)(&utf8[1], 1, srce + 1, r2, regs);

      /* WellformednessChecking */
      if(wfc)
      {
        if(utf8[0] == 0xe0)
        {
          if(utf8[1] < 0xa0 || utf8[1] > 0xbf || utf8[2] < 0x80 || utf8[2] > 0xbf)
          {
            regs->psw.cc = 2;
            return;
          }
        }
        if((utf8[0] >= 0xe1 && utf8[0] <= 0xec) || (utf8[0] >= 0xee && utf8[0] < 0xef))
        {
          if(utf8[1] < 0x80 || utf8[1] > 0xbf || utf8[2] < 0x80 || utf8[2] > 0xbf)
          {
            regs->psw.cc = 2;
            return;
          }
        }
        if(utf8[0] == 0xed)
        {
          if(utf8[1] < 0x80 || utf8[1] > 0x9f || utf8[2] < 0x80 || utf8[2] > 0xbf)
          {
            regs->psw.cc = 2;
            return;
          }
        }
      }

      /* xlate range e00000-efffff */
      /* 1110abcd 10efghij 10klmnop -> 00000000 00000000 abcdefgh ijklmnop */
      utf32[1] = 0x00;
      utf32[2] = (utf8[0] << 4) & ((utf8[1] & 0x3c) >> 2);
      utf32[3] = (utf8[1] << 6) & (utf8[2] & 0x3f);
      read = 3;
    }
    else if(utf8[0] >= 0xf0 && utf8[0] <= 0xf7)
    {
      /* WellFormednessChecking */
      if(wfc)
      {
        if(utf8[0] > 0xf4)
        {
          regs->psw.cc = 2;
          return;
        }
      }

      /* Check end of source */
      if(srcelen < 4)
      {
        regs->psw.cc = 0;   /* Strange but stated in POP */
        return;
      }

      /* Get the next 3 bytes */
      ARCH_DEP(vfetchc)(&utf8[1], 2, srce + 1, r2, regs);

      /* WellFormdnessChecking */
      if(wfc)
      {
        if(utf8[0] == 0xf0)
        {
          if(utf8[1] < 0x90 || utf8[1] > 0xbf || utf8[2] < 0x80 || utf8[2] > 0xbf || utf8[3] < 0x80 || utf8[3] > 0xbf)
          {
            regs->psw.cc = 2;
            return;
          }
        }
        if(utf8[0] >= 0xf1 && utf8[0] <= 0xf3)
        {
          if(utf8[1] < 0x80 || utf8[1] > 0xbf || utf8[2] < 0x80 || utf8[2] > 0xbf || utf8[3] < 0x80 || utf8[3] > 0xbf)
          {
            regs->psw.cc = 2;
            return;
          }
        }
        if(utf8[0] == 0xf4)
        {
          if(utf8[1] < 0x80 || utf8[1] > 0x8f || utf8[2] < 0x80 || utf8[2] > 0xbf || utf8[3] < 0x80 || utf8[3] > 0xbf)
          {
            regs->psw.cc = 2;
            return;
          }
        }
      }

      /* xlate range f0000000-f7000000 */
      /* 1110uvw 10xyefgh 10ijklmn 10opqrst -> 00000000 000uvwxy efghijkl mnopqrst */
      utf32[1] = ((utf8[0] & 0x07) << 2) & ((utf8[1] & 0x30) >> 4);
      utf32[2] = (utf8[1] << 4) & ((utf8[2] & 0x3c) >> 2);
      utf32[3] = (utf8[2] << 6) & (utf8[3] & 0x3f);
      read = 4;
    }
    else
    {
      regs->psw.cc = 2;
      return;
    }

    /* Write and commit registers */
    ARCH_DEP(vstorec)(utf32, 3, dest, r1, regs);
    SET_GR_A(r1, regs, (dest + 4) & ADDRESS_MAXWRAP(regs));
    SET_GR_A(r1 + 1, regs, destlen - 4);
    SET_GR_A(r2, regs, (srce + read) & ADDRESS_MAXWRAP(regs));
    SET_GR_A(r2 + 1, regs, srcelen - read);

    xlated += read;
  }

  /* CPU determined number of characters reached */
  regs->psw.cc = 3;
}

/*-------------------------------------------------------------------*/
/* B9B1 CU24 - Convert UTF-16 to UTF-32                        [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(convert_utf16_to_utf32)
{
  VADR dest;                       /* Destination address            */
  GREG destlen;                    /* Destination length             */
  int r1;
  int r2;
  int read;                        /* Bytes read                     */
  VADR srce;                       /* Source address                 */
  GREG srcelen;                    /* Source length                  */
  BYTE utf16[4];                   /* utf16 character(s)             */
  BYTE utf32[4];                   /* utf328 character(s)            */
  BYTE uvwxy;                      /* Work value                     */
  int wfc;                         /* Well-Formedness-Checking (W)   */
  int xlated;                      /* characters translated          */

  RRE(inst, regs, r1, r2);
  ODD2_CHECK(r1, r2, regs);

  /* Get paramaters */
  dest = regs->GR(r1) & ADDRESS_MAXWRAP(regs);
  destlen = GR_A(r1 + 1, regs);
  srce = regs->GR(r2) & ADDRESS_MAXWRAP(regs);
  srcelen = GR_A(r2 + 1, regs);
  if(inst[1] & 0x10)
    wfc = 1;
  else
    wfc = 0;

  /* Every valid utf-32 starts with 0x00 */
  utf32[0] = 0x00;

  /* Initialize number of translated charachters */
  xlated = 0;
  while(xlated < 4096)
  {
    /* Check end of source or destination */
    if(srcelen < 2)
    {
      regs->psw.cc = 0;
      return;
    }
    if(destlen < 4)
    {
      regs->psw.cc = 1;
        return;
    }

    /* Fetch 2 bytes */
    ARCH_DEP(vfetchc)(utf16, 1, srce, r2, regs);
    if(utf16[0] <= 0xd7 || utf16[0] >= 0xdc)
    {
      /* xlate range 0000-d7fff and dc00-ffff */
      /* abcdefgh ijklmnop -> 00000000 00000000 abcdefgh ijklmnop */
      utf32[1] = 0x00;
      utf32[2] = utf16[0];
      utf32[3] = utf16[1];
      read = 2;
    }
    else
    {
      /* Check end of source */
      if(srcelen < 4)
      {
        regs->psw.cc = 0;   /* Strange but stated in POP */
        return;
      }

      /* Fetch another 2 bytes */
      ARCH_DEP(vfetchc)(&utf16[2], 1, srce, r2, regs);

      /* WellFormednessChecking */
      if(wfc)
      {
        if(utf16[2] < 0xdc && utf16[2] > 0xdf)
        {
          regs->psw.cc = 2;
          return;
        }
      }

      /* xlate range d800-dbff */
      /* 110110ab cdefghij 110111kl mnopqrst -> 00000000 000uvwxy efghijkl mnopqrst */
      /* 000uvwxy = 0000abcde + 1 */
      uvwxy = (((utf16[0] & 0x03) << 2) | (utf16[1] >> 6)) + 1;
      utf32[1] = uvwxy;
      utf32[2] = (utf16[1] << 2) | (utf16[2] & 0x03);
      utf32[3] = utf16[3];
      read = 4;
    }

    /* Write and commit registers */
    ARCH_DEP(vstorec)(utf32, 3, dest, r1, regs);
    SET_GR_A(r1, regs, (dest + 4) & ADDRESS_MAXWRAP(regs));
    SET_GR_A(r1 + 1, regs, destlen - 4);
    SET_GR_A(r2, regs, (srce + read) & ADDRESS_MAXWRAP(regs));
    SET_GR_A(r2 + 1, regs, srcelen - read);

    xlated += read;
  }

  /* CPU determined number of characters reached */
  regs->psw.cc = 3;
}

/*-------------------------------------------------------------------*/
/* B9B2 CU41 - Convert UTF-32 to UTF-8                         [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(convert_utf32_to_utf8)
{
  VADR dest;                       /* Destination address            */
  GREG destlen;                    /* Destination length             */
  int r1;
  int r2;
  VADR srce;                       /* Source address                 */
  GREG srcelen;                    /* Source length                  */
  BYTE utf32[4];                   /* utf32 character(s)             */
  BYTE utf8[4];                    /* utf8 character(s)              */
  int write;                       /* Bytes written                  */
  int xlated;                      /* characters translated          */

  RRE(inst, regs, r1, r2);
  ODD2_CHECK(r1, r2, regs);

  /* Get paramaters */
  dest = regs->GR(r1) & ADDRESS_MAXWRAP(regs);
  destlen = GR_A(r1 + 1, regs);
  srce = regs->GR(r2) & ADDRESS_MAXWRAP(regs);
  srcelen = GR_A(r2 + 1, regs);

  /* Initialize number of translated charachters */
  xlated = 0;
  write = 0;
  while(xlated < 4096)
  {
    /* Check end of source or destination */
    if(srcelen < 4)
    {
      regs->psw.cc = 0;
      return;
    }
    if(destlen < 1)
    {
      regs->psw.cc = 1;
      return;
    }

    /* Get 4 bytes */
    ARCH_DEP(vfetchc)(utf32, 3, srce, r2, regs);

    if(utf32[0] != 0x00)
    {
      regs->psw.cc = 2;
      return;
    }
    else if(utf32[1] == 0x00)
    {
      if(utf32[2] == 0x00)
      {
        if(utf32[3] <= 0x7f)
        {
          /* xlate range 00000000-0000007f */
          /* 00000000 00000000 00000000 0jklmnop -> 0jklmnop */
          utf8[0] = utf32[3];
          write = 1;
        }
      }
      else if(utf32[2] <= 0x07)
      {
        /* Check destination length */
        if(destlen < 2)
        {
          regs->psw.cc = 1;
          return;
        }

        /* xlate range 00000080-000007ff */
        /* 00000000 00000000 00000fgh ijklmnop -> 110fghij 10klmnop */
        utf8[0] = 0xc0 | (utf32[2] << 2) | (utf32[2] >> 6);
        utf8[1] = 0x80 | (utf32[2] & 0x3f);
        write = 2;
      }
      else if(utf32[2] <= 0xd7 || utf32[2] > 0xdc)
      {
        /* Check destination length */
        if(destlen < 3)
        {
          regs->psw.cc = 1;
          return;
        }

        /* xlate range 00000800-0000d7ff and 0000dc00-0000ffff */
        /* 00000000 00000000 abcdefgh ijklnmop -> 1110abcd 10efghij 10klmnop */
        utf8[0] = 0xe0 | (utf32[2] >> 4);
        utf8[1] = 0x80 | ((utf32[2] & 0x0f) << 2) | (utf32[3] >> 6);
        utf8[2] = 0x80 | (utf32[3] & 0x3f);
        write = 3;
      }
      else
      {
        regs->psw.cc = 2;
        return;
      }
    }
    else if(utf32[1] >= 0x01 && utf32[1] <= 0x10)
    {
      /* Check destination length */
      if(destlen < 4)
      {
        regs->psw.cc = 1;
        return;
      }

      /* xlate range 00010000-0010ffff */
      /* 00000000 000uvwxy efghijkl mnopqrst -> 11110uvw 10xyefgh 10ijklmn 10opqrst */
      utf8[0] = 0xf0 | (utf32[1] >> 2);
      utf8[1] = 0x80 | ((utf32[1] & 0x03) << 4) | (utf32[2] >> 4);
      utf8[2] = 0x80 | ((utf32[2] & 0x0f) < 2) | (utf32[3] >> 6);
      utf8[3] = 0x80 | (utf32[3] & 0x3f);
      write = 4;
    }
    else
    {
      regs->psw.cc = 2;
      return;
    }

    /* Write and commit registers */
    ARCH_DEP(vstorec)(utf8, write - 1, dest, r1, regs);
    SET_GR_A(r1, regs, (dest + write) & ADDRESS_MAXWRAP(regs));
    SET_GR_A(r1 + 1, regs, destlen - write);
    SET_GR_A(r2, regs, (srce + 4) & ADDRESS_MAXWRAP(regs));
    SET_GR_A(r2 + 1, regs, srcelen - 4);

    xlated += 4;
  }

  /* CPU determined number of characters reached */
  regs->psw.cc = 3;
}

/*-------------------------------------------------------------------*/
/* B9B3 CU42 - Convert UTF-32 to UTF-16                        [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(convert_utf32_to_utf16)
{
  VADR dest;                       /* Destination address            */
  GREG destlen;                    /* Destination length             */
  int r1;
  int r2;
  VADR srce;                       /* Source address                 */
  GREG srcelen;                    /* Source length                  */
  BYTE utf16[4];                   /* utf16 character(s)             */
  BYTE utf32[4];                   /* utf32 character(s)             */
  int write;                       /* Bytes written                  */
  int xlated;                      /* characters translated          */
  BYTE zabcd;                      /* Work value                     */

  RRE(inst, regs, r1, r2);
  ODD2_CHECK(r1, r2, regs);

  /* Get paramaters */
  dest = regs->GR(r1) & ADDRESS_MAXWRAP(regs);
  destlen = GR_A(r1 + 1, regs);
  srce = regs->GR(r2) & ADDRESS_MAXWRAP(regs);
  srcelen = GR_A(r2 + 1, regs);

  /* Initialize number of translated charachters */
  xlated = 0;
  while(xlated < 4096)
  {
    /* Check end of source or destination */
    if(srcelen < 4)
    {
      regs->psw.cc = 0;
      return;
    }
    if(destlen < 2)
    {
      regs->psw.cc = 1;
      return;
    }

    /* Get 4 bytes */
    ARCH_DEP(vfetchc)(utf32, 3, srce, r2, regs);

    if(utf32[0] != 0x00)
    {
      regs->psw.cc = 2;
      return;
    }
    else if(utf32[1] == 0x00 && (utf32[2] <= 0xd7 || utf32[2] >= 0xdc))
    {
      /* xlate range 00000000-0000d7ff and 0000dc00-0000ffff */
      /* 00000000 00000000 abcdefgh ijklmnop -> abcdefgh ijklmnop */
      utf16[0] = utf32[2];
      utf16[1] = utf32[3];
      write = 2;
    }
    else if(utf32[1] >= 0x01 && utf32[1] <= 0x10)
    {
      /* Check end of destination */
      if(destlen < 4)
      {
        regs->psw.cc = 1;
        return;
      }

      /* xlate range 00010000-0010ffff */
      /* 00000000 000uvwxy efghijkl mnopqrst -> 110110ab cdefghij 110111kl mnopqrst */
      /* 000zabcd = 000uvwxy - 1 */
      zabcd = (utf32[1] - 1) & 0x0f;
      utf16[0] = 0xd8 | (zabcd >> 2);
      utf16[1] = (zabcd << 6) | (utf32[2] >> 2);
      utf16[2] = 0xd9 | (utf32[2] & 0x03);
      utf16[3] = utf32[3];
      write = 4;
    }
    else
    {
      regs->psw.cc = 2;
      return;
    }

    /* Write and commit registers */
    ARCH_DEP(vstorec)(utf16, write - 1, dest, r1, regs);
    SET_GR_A(r1, regs, (dest + write) & ADDRESS_MAXWRAP(regs));
    SET_GR_A(r1 + 1, regs, destlen - write);
    SET_GR_A(r2, regs, (srce + 4) & ADDRESS_MAXWRAP(regs));
    SET_GR_A(r2 + 1, regs, srcelen - 4);

    xlated += 4;
  }

  /* CPU determined number of characters reached */
  regs->psw.cc = 3;
}

/*-------------------------------------------------------------------*/
/* B9B1 STSTU - Search String Unicode                          [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(search_string_unicode)
{
  VADR addr1, addr2;                    /* End/start addresses       */
  int i;                                /* Loop counter              */
  int r1, r2;                           /* Values of R fields        */
  U16 sbyte;                            /* String character          */
  U16 termchar;                         /* Terminating character     */

  RRE(inst, regs, r1, r2);

  /* Program check if bits 0-15 of register 0 not zero */
  if(regs->GR_L(0) & 0xFFFF0000)
    ARCH_DEP(program_interrupt) (regs, PGM_SPECIFICATION_EXCEPTION);

  /* Load string terminating character from register 0 bits 16-31 */
  termchar = (U16) regs->GR(0);

  /* Determine the operand end and start addresses */
  addr1 = regs->GR(r1) & ADDRESS_MAXWRAP(regs);
  addr2 = regs->GR(r2) & ADDRESS_MAXWRAP(regs);

  /* Search up to 256 bytes or until end of operand */
  for(i = 0; i < 0x100; i++)
  {
    /* If operand end address has been reached, return condition
       code 2 and leave the R1 and R2 registers unchanged */
    if(addr2 == addr1)
    {
      regs->psw.cc = 2;
      return;
    }

    /* Fetch 2 bytes from the operand */
    sbyte = ARCH_DEP(vfetch2)(addr2, r2, regs );

    /* If the terminating character was found, return condition
       code 1 and load the address of the character into R1 */
    if(sbyte == termchar)
    {
      SET_GR_A(r1, regs, addr2);
      regs->psw.cc = 1;
      return;
    }

    /* Increment operand address */
    addr2 += 2;
    addr2 &= ADDRESS_MAXWRAP(regs);

  } /* end for(i) */

  /* Set R2 to point to next character of operand */
  SET_GR_A(r2, regs, addr2);

  /* Return condition code 3 */
  regs->psw.cc = 3;
}

/*-------------------------------------------------------------------*/
/* D0 TRTR - Translate and Test Reverse                         [SS] */
/*-------------------------------------------------------------------*/
DEF_INST(translate_and_test_reverse)
{
  int b1, b2;                           /* Values of base field      */
  int cc = 0;                           /* Condition code            */
  BYTE dbyte;                           /* Byte work areas           */
  VADR effective_addr1;
  VADR effective_addr2;                 /* Effective addresses       */
  int i;                                /* Integer work areas        */
  int l;                                /* Lenght byte               */
  BYTE sbyte;                           /* Byte work areas           */

  SS_L(inst, regs, l, b1, effective_addr1, b2, effective_addr2);

  /* Process first operand from right to left*/
  for(i = 0; i <= l; i++)
  {
    /* Fetch argument byte from first operand */
    dbyte = ARCH_DEP(vfetchb)(effective_addr1, b1, regs);

    /* Fetch function byte from second operand */
    sbyte = ARCH_DEP(vfetchb)((effective_addr2 + dbyte) & ADDRESS_MAXWRAP(regs), b2, regs);

    /* Test for non-zero function byte */
    if(sbyte != 0)
    {
      /* Store address of argument byte in register 1 */
#if defined(FEATURE_ESAME)
      if(regs->psw.amode64)
        regs->GR_G(1) = effective_addr1;
      else
#endif
      if(regs->psw.amode)
      {
        /* Note: TRTR differs from TRT in 31 bit mode.
           TRTR leaves bit 32 unchanged, TRT clears bit 32 */
        regs->GR_L(1) &= 0x80000000;
        regs->GR_L(1) |= effective_addr1;
      }
      else
        regs->GR_LA24(1) = effective_addr1;

      /* Store function byte in low-order byte of reg.2 */
      regs->GR_LHLCL(2) = sbyte;

      /* Set condition code 2 if argument byte was last byte
         of first operand, otherwise set condition code 1 */
      cc = (i == l) ? 2 : 1;

      /* Terminate the operation at this point */
      break;

    } /* end if(sbyte) */

    /* Decrement first operand address */
    effective_addr1--; /* Another difference with TRT */
    effective_addr1 &= ADDRESS_MAXWRAP(regs);

  } /* end for(i) */

  /* Update the condition code */
  regs->psw.cc = cc;
}
#endif /*defined(FEATURE_EXTENDED_TRANSLATION_FACILITY_3)*/

#if !defined(_GEN_ARCH)

#if defined(_ARCHMODE2)
 #define  _GEN_ARCH _ARCHMODE2
 #include "general2.c"
#endif

#if defined(_ARCHMODE3)
 #undef   _GEN_ARCH
 #define  _GEN_ARCH _ARCHMODE3
 #include "general2.c"
#endif

#endif /*!defined(_GEN_ARCH)*/
