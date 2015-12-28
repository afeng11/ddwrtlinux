/**
 * @file IxFeatureCtrl.h
 *
 * @date 26-Oct-2005

 * @brief This file contains the public API of the IXP Feature Control
 *        component.
 *
 * 
 * @par
 * IXP400 SW Release Crypto version 2.4
 * 
 * -- Copyright Notice --
 * 
 * @par
 * Copyright (c) 2001-2007, Intel Corporation.
 * All rights reserved.
 * 
 * @par
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * 
 * @par
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * 
 * @par
 * -- End of Copyright Notice --
*/
/* ------------------------------------------------------
   Doxygen group definitions
   ------------------------------------------------------ */
/**
 * @defgroup IxFeatureCtrlAPI Intel (R) IXP400 Software Feature Control (featureCtrl) Public API
 *
 * @brief The Public API for the IXP400 Feature Control.
 * 
 */

/**
 * @defgroup IxFeatureCtrlSwConfig Intel (R) IXP400 Software Configuration for featureCtrl Component
 *
 * @ingroup IxFeatureCtrlAPI 
 *
 * @brief This section describes software configuration in access component. 
 *
 * The configuration can be changed at run-time. ixFeatureCtrlSwConfigurationCheck() will be used across 
 * applicable access layer components to check the configuration. ixFeatureCtrlSwConfigurationWrite() is used 
 * to write the software configuration.
 *
 * @note <b>All software configurations are default to be enabled.</b> 
 *
 */

#ifndef IXFEATURECTRL_H
#define IXFEATURECTRL_H

/*
 * User defined include files
 */
#include "IxOsal.h"

/*
 * #defines and macros
 */

/*************************************************************
 * The following are IxFeatureCtrlComponentCheck return values.
 ************************************************************/

/**
 * @ingroup IxFeatureCtrlAPI 
 *
 * @def  IX_FEATURE_CTRL_COMPONENT_DISABLED
 *
 * @brief Hardware Component is disabled/unavailable.
 *        Return status by ixFeatureCtrlComponentCheck()  
 */
#define  IX_FEATURE_CTRL_COMPONENT_DISABLED 0

/**
 * @ingroup IxFeatureCtrlAPI 
 *
 * @def  IX_FEATURE_CTRL_COMPONENT_ENABLED
 *
 * @brief Hardware Component is available. 
 *        Return status by ixFeatureCtrlComponentCheck()  
 */
#define  IX_FEATURE_CTRL_COMPONENT_ENABLED  1

/***********************************************************************************
 * Product ID in Intel XScale(R) Processor CP15 - Register 0
 *  - It contains information on the maximum Intel XScale(R) Processor Frequency and
 *    Silicon Stepping.  
 *  - Intel XScale(R) Processor Frequency Id indicates only the maximum Intel XScale(R) Processor frequency
 *    achievable and not the running Intel XScale(R) Processor frequency (maybe stepped down).    
 *  - The register is read by using ixFeatureCtrlProductIdRead.
 *  - Usage example: 
 *          productId = ixFeatureCtrlProductIdRead();
 *          if( (productId & IX_FEATURE_CTRL_SILICON_STEPPING_MASK) == 
 *              IX_FEATURE_CTRL_SILICON_TYPE_A0 )
 *          if( (productId & IX_FEATURE_CTRL_XSCALE_FREQ_MASK) == 
 *              IX_FEATURE_CTRL_XSCALE_FREQ_533 )    
 *
 *  Register mapping for IXP42X & IXP46X processors are shown below.
 * 
 *  31 28 27 24 23 20 19 16 15 12 11        9 8                                 4 3              0  
 *  ---------------------------------------------------------------------------------------------- 
 * | 0x6 | 0x9 | 0x0 | 0x5 | 0x4 | Device ID | Intel XScale(R) Processor Freq Id | Si Stepping Id |    
 *  ----------------------------------------------------------------------------------------------
 *
 *   Maximum Achievable Intel XScale(R) Processor Frequency Id :  533MHz  - 0x1C 
 *                                                  400MHz  - 0x1D 
 *                                                  266MHz  - 0x1F
 *
 *   <b>THE CORE FREQUENCY ID IS NOT APPLICABLE TO IXP46X <\b>
 *  
 *   The above is applicable to IXP42X processor only. 
 * 
 *  Si Stepping Id            :  A       - 0x0    
 *                               B       - 0x1 
 *  
 *  Intel XScale(R) Processor freq Id - Device ID [11:9] : IXP42X processor - 0x0
 *                                                         IXP46X processor - 0x1
 *
 *  Register mapping for IXP43X processor is shown below.
 *
 *  31 28 27 24 23 20 19 16 15      13 12       10 9                    4 3              0  
 *  -------------------------------------------------------------------------------------- 
 * | 0x6 | 0x9 | 0x0 | 0x5 | Core Gen | Core Rev  |       Product ID     | Si Stepping Id |    
 *  --------------------------------------------------------------------------------------
 * 
 *  Core Gen       : 0x2
 *  Core Rev       : 0x0
 *  Product ID     : 0x4
 *  Si Stepping Id : 0x1
 *
 *************************************************************************************/

/**
 * @ingroup IxFeatureCtrlAPI 
 *
 * @def IX_FEATURE_CTRL_SILICON_TYPE_A0
 *
 * @brief This is the value of A0 Silicon in product ID. 
 */
#define IX_FEATURE_CTRL_SILICON_TYPE_A0   0

/**
 * @ingroup IxFeatureCtrlAPI 
 *
 * @def IX_FEATURE_CTRL_SILICON_TYPE_B0
 *
 * @brief This is the value of B0 Silicon in product ID.
 */
#define IX_FEATURE_CTRL_SILICON_TYPE_B0   1


#if defined(__ixp42X) 
/**
 * @ingroup IxFeatureCtrlAPI 
 *
 * @def IX_FEATURE_CTRL_SILICON_TYPE_B1
 *
 * @brief This is the value of B1 Silicon in product ID for IXP 425.
 */
#define IX_FEATURE_CTRL_SILICON_TYPE_B1   2 
#endif /* __ixp42X */


/**
 * @ingroup IxFeatureCtrlAPI 
 *
 * @def IX_FEATURE_CTRL_SILICON_STEPPING_MASK
 *
 * @brief This is the mask of silicon stepping in product ID. 
 */
#define IX_FEATURE_CTRL_SILICON_STEPPING_MASK  0xF 

/**
 * @ingroup IxFeatureCtrlAPI
 *
 * @def IX_FEATURE_CTRL_DEVICE_TYPE_MASK
 *
 * @brief This is the mask of silicon stepping in product ID.
 */
#if defined(__ixp43X)
#define IX_FEATURE_CTRL_DEVICE_TYPE_MASK  (0x3F)
#else
#define IX_FEATURE_CTRL_DEVICE_TYPE_MASK  (0x7)
#endif /* __ixp43X */


/**
 * @ingroup IxFeatureCtrlAPI
 *
 * @def IX_FEATURE_CTRL_DEVICE_TYPE_OFFSET
 *
 * @brief This is the mask of silicon stepping in product ID.
 */
#if defined(__ixp43X)
#define IX_FEATURE_CTRL_DEVICE_TYPE_OFFSET  4
#else
#define IX_FEATURE_CTRL_DEVICE_TYPE_OFFSET  9
#endif /* __ixp43X */

/**
 * @ingroup IxFeatureCtrlAPI 
 *
 * @def IX_FEATURE_CTRL_XSCALE_FREQ_533
 *
 * @brief This is the value of 533MHz Intel XScale(R) Processor in product ID.
 */
#define IX_FEATURE_CTRL_XSCALE_FREQ_533  ((0x1C)<<4)

/**
 * @ingroup IxFeatureCtrlAPI 
 *
 * @def IX_FEATURE_CTRL_XSCALE_FREQ_400
 *
 * @brief This is the value of 400MHz Intel XScale(R) Processor in product ID.
 */
#define IX_FEATURE_CTRL_XSCALE_FREQ_400  ((0x1D)<<4)

/**
 * @ingroup IxFeatureCtrlAPI 
 *
 * @def IX_FEATURE_CTRL_XSCALE_FREQ_266
 *
 * @brief This is the value of 266MHz Intel XScale(R) Processor in product ID.
 */
#define IX_FEATURE_CTRL_XSCALE_FREQ_266 ((0x1F)<<4)   

/**
 * @ingroup IxFeatureCtrlAPI 
 *
 * @def IX_FEATURE_CTRL_XSCALE_FREQ_MASK
 *
 * @brief This is the mask of Intel XScale(R) Processor in product ID.
 */
#define IX_FEATURE_CTRL_XSCALE_FREQ_MASK ((0xFF)<<4)  

/**
 * @ingroup IxFeatureCtrlAPI 
 *
 * @def IX_FEATURECTRL_REG_UTOPIA_32PHY
 *
 * @brief Maximum  UTOPIA PHY available is 32.  
 * 
 */
#define IX_FEATURECTRL_REG_UTOPIA_32PHY  0x0

/**
 * @ingroup IxFeatureCtrlAPI 
 *
 * @def IX_FEATURECTRL_REG_UTOPIA_16PHY
 *
 * @brief Maximum  UTOPIA PHY available is 16.  
 * 
 */
#define IX_FEATURECTRL_REG_UTOPIA_16PHY  0x1

/**
 * @ingroup IxFeatureCtrlAPI 
 *
 * @def IX_FEATURECTRL_REG_UTOPIA_8PHY
 *
 * @brief Maximum  UTOPIA PHY available to is 8.  
 * 
 */
#define IX_FEATURECTRL_REG_UTOPIA_8PHY   0x2

/**
 * @ingroup IxFeatureCtrlAPI 
 *
 * @def IX_FEATURECTRL_REG_UTOPIA_4PHY
 *
 * @brief Maximum  UTOPIA PHY available to is 4.  
 * 
 */
#define IX_FEATURECTRL_REG_UTOPIA_4PHY   0x3

#if defined(__ixp46X) 
/**
 * @ingroup IxFeatureCtrlAPI
 *
 * @def IX_FEATURECTRL_REG_XSCALE_533FREQ
 *
 * @brief Maximum  frequency available to Intel (R) IXP46X Product Line of Network
 * Processors is 533 MHz.
 *
 */
#define IX_FEATURECTRL_REG_XSCALE_533FREQ   0x0

/**
 * @ingroup IxFeatureCtrlAPI
 *
 * @def IX_FEATURECTRL_REG_XSCALE_667FREQ
 *
 * @brief Maximum  frequency available to Intel (R) IXP46X Product Line of Network
 * Processors is 667 MHz.
 *
 */
#define IX_FEATURECTRL_REG_XSCALE_667FREQ   0x1

/**
 * @ingroup IxFeatureCtrlAPI
 *
 * @def IX_FEATURECTRL_REG_XSCALE_400FREQ
 *
 * @brief Maximum  frequency available to Intel (R) IXP46X Product Line of Network
 * Processors is 400 MHz.
 *
 */
#define IX_FEATURECTRL_REG_XSCALE_400FREQ   0x2

/**
 * @ingroup IxFeatureCtrlAPI
 *
 * @def IX_FEATURECTRL_REG_XSCALE_266FREQ
 *
 * @brief Maximum  frequency available to Intel (R) IXP46X Product Line of Network
 * Processors is 266 MHz.
 *
 */
#define IX_FEATURECTRL_REG_XSCALE_266FREQ   0x3

#endif /* __ixp46X */

#if defined(__ixp43X) 
/**
 * @ingroup IxFeatureCtrlAPI
 *
 * @def IX_FEATURECTRL_REG_XSCALE_533FREQ
 *
 * @brief Maximum  frequency available to Intel (R) IXP46X Product Line of Network
 * Processors is 533 MHz.
 *
 */
#define IX_FEATURECTRL_REG_XSCALE_533FREQ   0x0

/**
 * @ingroup IxFeatureCtrlAPI
 *
 * @def IX_FEATURECTRL_REG_XSCALE_400FREQ
 *
 * @brief Maximum  frequency available to Intel (R) IXP46X Product Line of Network
 * Processors is 400 MHz.
 *
 */
#define IX_FEATURECTRL_REG_XSCALE_400FREQ   0x1

/**
 * @ingroup IxFeatureCtrlAPI
 *
 * @def IX_FEATURECTRL_REG_XSCALE_667FREQ
 *
 * @brief Maximum  frequency available to Intel (R) IXP46X Product Line of Network
 * Processors is 667 MHz.
 *
 */
#define IX_FEATURECTRL_REG_XSCALE_667FREQ   0x2

/**
 * @ingroup IxFeatureCtrlAPI
 *
 * @def IX_FEATURECTRL_REG_XSCALE_266FREQ
 *
 * @brief Maximum  frequency available to Intel (R) IXP46X Product Line of Network
 * Processors is 266 MHz.
 *
 */
#define IX_FEATURECTRL_REG_XSCALE_266FREQ   0x3

#endif /* __ixp43X */



/**
 * @ingroup IxFeatureCtrlAPI
 *
 * @def IX_FEATURECTRL_COMPONENT_NOT_AVAILABLE
 *
 * @brief Component selected is not available for device
 *
 */
#define IX_FEATURECTRL_COMPONENT_NOT_AVAILABLE  0x0000

/**
 * @ingroup IxFeatureCtrlAPI
 *
 * @def IX_FEATURECTRL_COMPONENT_ALWAYS_AVAILABLE
 *
 * @brief Component selected is not available for device
 *
 */
#define IX_FEATURECTRL_COMPONENT_ALWAYS_AVAILABLE  0xffff

/**
 * @defgroup IxFeatureCtrlSwConfig Intel (R) IXP400 Software Configuration for Access Component
 *
 * @ingroup IxFeatureCtrlAPI 
 *
 * @brief This section describes software configuration in access component. The
 *        configuration can be changed at run-time. ixFeatureCtrlSwConfigurationCheck( )
 *        will be used across applicable access component to check the configuration.
 *        ixFeatureCtrlSwConfigurationWrite( ) is used to write the software configuration.
 *
 * @note <b>All software configurations are default to be enabled.</b> 
 *
 * @{
 */
/**
 * @ingroup IxFeatureCtrlSwConfig
 *
 * @def IX_FEATURE_CTRL_SWCONFIG_DISABLED
 *
 * @brief Software configuration is disabled. 
 * 
 */
#define IX_FEATURE_CTRL_SWCONFIG_DISABLED 0  

/**
 * @ingroup IxFeatureCtrlSwConfig
 *
 * @def IX_FEATURE_CTRL_SWCONFIG_ENABLED
 *
 * @brief Software configuration is enabled. 
 * 
 */
#define IX_FEATURE_CTRL_SWCONFIG_ENABLED 1  

/**
 * Section for enums
 **/

/**
 * @ingroup IxFeatureCtrlBuildDevice
 *
 * @enum IxFeatureCtrlBuildDevice
 *
 * @brief Indicates software build type.
 *
 */
typedef enum
{
    IX_FEATURE_CTRL_SW_BUILD_IXP42X = 0, /**<Build type is IXP42X */
    IX_FEATURE_CTRL_SW_BUILD_IXP46X,     /**<Build type is IXP46X */
    IX_FEATURE_CTRL_SW_BUILD_IXP43X,     /**<Build type is IXP43X */
    IX_FEATURE_CTRL_SW_BUILD_MAX      /**<Max build types */
} IxFeatureCtrlBuildDevice;

/**
 * @ingroup IxFeatureCtrlSwConfig
 *
 * @enum IxFeatureCtrlSwConfig
 *
 * @brief Enumeration for software configuration in access components.
 *
 * Entry for new run-time software configuration should be added here.
 */
typedef enum
{
    IX_FEATURECTRL_ETH_LEARNING = 0,       /**< EthDB Learning Feature */
    IX_FEATURECTRL_ORIGB0_DISPATCHER,  /**< IXP42X B0 Silicon and IXP46X processor dispatcher without 
                                            livelock prevention functionality Feature */
    IX_FEATURECTRL_SWCONFIG_MAX    /**< Maximum boudary for IxFeatureCtrlSwConfig  */
} IxFeatureCtrlSwConfig;

/************************************************************************
 * IXP400 Feature Control Register
 * - It contains the information (available/unavailable) of IXP425&IXP46X
 *   hardware components in their corresponding bit location.
 * - Bit value of 0 means the hardware component is available
 *   or not software disabled. Hardware component that is available
 *   can be software disabled.
 * - Bit value of 1 means the hardware is unavailable or software
 *   disabled.Hardware component that is unavailable cannot be software
 *   enabled.
 * - Use ixFeatureCtrlHwCapabilityRead() to read the hardware component's
 *   availability.
 * - Use ixFeatureCtrlRead() to get the current IXP425/IXP46X feature control
 *   register value.
 *
 *   Bit            Field Description (Hardware Component Availability)
 *   ---            ---------------------------------------------------
 *    0             RComp Circuitry
 *    1             USB Controller
 *    2             Hashing Coprocessor
 *    3             AES Coprocessor
 *    4             DES Coprocessor
 *    5             HDLC Coprocessor
 *    6             AAL Coprocessor         - Always available in IXP46X
 *    7             HSS Coprocesspr
 *    8             Utopia Coprocessor
 *    9             Ethernet 0 Coprocessor
 *   10             Ethernet 1 Coprocessor
 *   11             NPE A
 *   12             NPE B
 *   13             NPE C
 *   14             PCI Controller
 *   15             ECC/TimeSync Coprocessor -  Only applicable to IXP46X
 *   15             ECC Only -  Only applicable to IXP43X
 *  16-17           Utopia PHY Limit Status : 0x0 - 32 PHY
 *                                            0x1 - 16 PHY
 *                                            0x2 -  8 PHY
 *                                            0x3 -  4 PHY
 *
 *  Portions below are only applicable to IXP46X
 *   18             USB Host Coprocessor
 *   19             NPE A Ethernet - 0 for Enable if Utopia = 1
 *   20             NPE B Ethernet coprocessor 1-3.
 *   21             RSA Crypto Block coprocessor.
 *  22-23           Processor frequency : 0x0 - 533 MHz
 *                                        0x1 - 667 MHz
 *                                        0x2 - 400 MHz
 *                                        0x3 - 266 MHz
 *
 *  Portions below are only applicable to IXP43X
 *   18             USB Host Controller Disable( Both 0 & 1)
 *   19             NPE A Ethernet - 0 for Enable if Utopia = 1
 *   20-21          Reserved
 *   22-23          Processor frequency : 0x0 - 533 MHz
 *                                        0x1 - 400 MHz
 *                                        0x2 - 667 MHz
 *                                        0x3 - 266 MHz
 *
 *   24              Crystal Oscillator Bypass
 *                      0 = Crystal oscillator Enabled (default)
 *                      1 = Crystal oscillator Bypass
 *  25-31           Reserved
 *
 ************************************************************************/
/*Section generic to IXP42X, IXP43X and IXP46X*/

/**
 * @ingroup IxFeatureCtrlAPI
 *
 * @enum IxFeatureCtrlComponentType
 *
 * @brief Enumeration for components available
 *
 */
typedef enum
{
    IX_FEATURECTRL_RCOMP = 0, /**<bit location for RComp Circuitry*/
    IX_FEATURECTRL_USB,       /**<bit location for USB Controller*/
    IX_FEATURECTRL_HASH,      /**<bit location for Hashing Coprocessor*/
    IX_FEATURECTRL_AES,       /**<bit location for AES Coprocessor*/
    IX_FEATURECTRL_DES,       /**<bit location for DES Coprocessor*/
    IX_FEATURECTRL_HDLC,      /**<bit location for HDLC Coprocessor*/
    IX_FEATURECTRL_AAL,       /**<bit location for AAL Coprocessor*/
    IX_FEATURECTRL_HSS,       /**<bit location for HSS Coprocessor*/
    IX_FEATURECTRL_UTOPIA,    /**<bit location for UTOPIA Coprocessor*/
    IX_FEATURECTRL_ETH0,      /**<bit location for Ethernet 0 Coprocessor*/
    IX_FEATURECTRL_ETH1,      /**<bit location for Ethernet 1 Coprocessor / In IXP43X refers to NPE C Ethernet Disable */
    IX_FEATURECTRL_NPEA,      /**<bit location for NPE A*/
    IX_FEATURECTRL_NPEB,      /**<bit location for NPE B*/
    IX_FEATURECTRL_NPEC,      /**<bit location for NPE C*/
    IX_FEATURECTRL_PCI,       /**<bit location for PCI Controller*/
    IX_FEATURECTRL_ECC_TIMESYNC,     /**<bit location for TimeSync Coprocessor (Applicable only to IXP46X) */
    IX_FEATURECTRL_UTOPIA_PHY_LIMIT, /**<bit location for Utopia PHY Limit Status*/
    IX_FEATURECTRL_UTOPIA_PHY_LIMIT_BIT2, /**<2nd bit of PHY limit status*/
    IX_FEATURECTRL_USB_HOST_CONTROLLER, /**<bit location for USB host controller*/
    IX_FEATURECTRL_NPEA_ETH,  /**<bit location for NPE-A Ethernet Disable*/
    IX_FEATURECTRL_NPEB_ETH,  /**<bit location for NPE-B Ethernet 1-3 Coprocessors Disable*/
    IX_FEATURECTRL_RSA,       /**<bit location for RSA Crypto block Coprocessors Disable*/
    IX_FEATURECTRL_XSCALE_MAX_FREQ, /**<bit location for Intel XScale(R) processor max frequency*/
    IX_FEATURECTRL_XSCALE_MAX_FREQ_BIT2, /**<2nd Intel XScale(R) processor  max freq bit NOT TO BE USED */
    IX_FEATURECTRL_CRYSTAL_OSC_BYPASS, /**< bit location for Crystal Oscillator Bypass */    
    IX_FEATURECTRL_MAX_COMPONENTS
} IxFeatureCtrlComponentType;

/**
 * @ingroup IxFeatureCtrlAPI 
 *
 * @def  IX_FEATURECTRL_ECC
 *
 * @brief This value indicates the bit location for ECC in IXP43X Feature Control
 *        register. The bit location for ECC is the same as IX_FEATURECTRL_ECC_TIMESYNC  
 *        bit in IXP46X Feature Control register.
 *
 */
#define IX_FEATURECTRL_ECC IX_FEATURECTRL_ECC_TIMESYNC

/**
 * @ingroup IxFeatureCtrlDeviceId
 *
 * @enum IxFeatureCtrlDeviceId
 *
 * @brief Enumeration for device type.
 *
 * @warning This enum is closely related to the npe image. Its format should comply
 *          with formats used in the npe image ImageID. This is indicated by the  
 *          first nibble of the image ID. This should also be in sync with the
 *          with what is defined in CP15.  Current available formats are
 *
 *          - IXP42X - 0000
 *          - IXP46X - 0001
 *          - IXP43X - 0004
 */

typedef enum
{
    IX_FEATURE_CTRL_DEVICE_TYPE_IXP42X = 0, /**<Device type is IXP42X */
    IX_FEATURE_CTRL_DEVICE_TYPE_IXP46X, /**<Device type is IXP46X */
    IX_FEATURE_CTRL_DEVICE_TYPE_IXP43X = 4, /**<Device type is IXP43X */
    IX_FEATURE_CTRL_DEVICE_TYPE_MAX /**<Max devices */
} IxFeatureCtrlDeviceId;


/**
 * @} addtogroup IxFeatureCtrlSwConfig
 */

/*
 * Typedefs
 */

/**
 * @ingroup IxFeatureCtrlAPI 
 * 
 * @typedef IxFeatureCtrlReg
 *
 * @brief Feature Control Register that contains hardware components'
 *        availability information.
 */
typedef UINT32 IxFeatureCtrlReg;

/**
 * @ingroup IxFeatureCtrlAPI 
 * 
 * @typedef IxFeatureCtrlProductId
 *
 * @brief Product ID of Silicon that contains Silicon Stepping and 
 *        Maximum Intel XScale(R) Processor Frequency information.  
 */
typedef UINT32 IxFeatureCtrlProductId;

/*
 * Prototypes for interface functions
 */

/**
 * @ingroup IxFeatureCtrlAPI
 *
 * @fn IxFeatureDeviceId ixFeatureCtrlDeviceRead (void)
 *
 * @brief This function gets the type of device that the software is currently running
 *        on
 *
 * This function reads the feature Ctrl register specifically to obtain the device id.
 * The definitions of the avilable IDs are as above.
 *
 * @return
 *      - IxFeatureCtrlDeviceId - the type of device currently running
 */
IxFeatureCtrlDeviceId
ixFeatureCtrlDeviceRead (void);

/**
 * @ingroup IxFeatureCtrlAPI
 *
 * @fn IxFeatureCtrlBuildDevice ixFeatureCtrlSoftwareBuildGet (void)
 *
 * @brief This function refers to  the value set by the compiler flag to determine
 *        the type of device the software is built for.
 *
 * The function reads the compiler flag to determine the device the software is
 * built for. When the user executes build in the command line, 
 * a compile time flag (__ixp42X/__ixp46X) is set. This API reads this 
 * flag and returns the software build type to the calling client.
 *
 * @return
 *      - IxFeatureCtrlBuildDevice - the type of device software is built for.
 */
IxFeatureCtrlBuildDevice
ixFeatureCtrlSoftwareBuildGet (void);

/**
 * @ingroup IxFeatureCtrlAPI 
 *
 * @fn IxFeatureCtrlReg ixFeatureCtrlHwCapabilityRead (void)
 * 
 * @brief This function reads out the hardware capability of a silicon type as defined in 
 * feature control register.This value is different from that returned by 
 * ixFeatureCtrlRead() because this function returns the actual hardware component
 * availability.     
 *
 * The bit location of each hardware component is defined above. 
 * A value of '1' in bit means the hardware component is not available.  A value of '0'   
 * means the hardware component is available.
 *
 * @return 
 *      - IxFeatureCtrlReg - the hardware capability of the device. 
 *
 * @warning
 *      - This function must not be called when the device is running as the result
 *        is undefined.    
 */ 
PUBLIC IxFeatureCtrlReg
ixFeatureCtrlHwCapabilityRead (void);

/**
 * @ingroup IxFeatureCtrlAPI 
 *
 * @fn IX_STATUS ixFeatureCtrlComponentCheck (IxFeatureCtrlComponentType componentType)
 * 
 * @brief This function will check the availability of hardware component specified
 *        as componentType value. 
 *
 *        Usage Example:<br> 
 *         -  if(IX_FEATURE_CTRL_COMPONENT_DISABLED != 
 *              ixFeatureCtrlComponentCheck(IX_FEATURECTRL_NPEA)) <br>
 *         -  if(IX_FEATURE_CTRL_COMPONENT_ENABLED == 
 *              ixFeatureCtrlComponentCheck(IX_FEATURECTRL_NPEB)) <br>
 *
 * This function is typically called during component initialization time. 
 *
 * @param componentType @ref IxFeatureCtrlComponentType [in] - the type of a component as
 *        defined above as IX_FEATURECTRL_XXX           

 *        
 * @return 
 *      - IX_FEATURE_CTRL_COMPONENT_ENABLED if component is available 
 *      - IX_FEATURE_CTRL_COMPONENT_DISABLED if component is unavailable            
 */ 
PUBLIC IX_STATUS
ixFeatureCtrlComponentCheck (IxFeatureCtrlComponentType componentType);

/**
 * @ingroup IxFeatureCtrlAPI 
 * 
 * @fn IxFeatureCtrlProductId ixFeatureCtrlProductIdRead (void)
 * 
 * @brief This function will return the device product ID.
 *                                                
 * @return 
 *      - IxFeatureCtrlProductId - the value of product ID.
 *
 */ 
PUBLIC IxFeatureCtrlProductId
ixFeatureCtrlProductIdRead (void) ;

/**
 * @ingroup IxFeatureCtrlAPI 
 *
 * @fn IX_STATUS ixFeatureCtrlSwConfigurationCheck (IxFeatureCtrlSwConfig swConfigType)
 * 
 * @brief This function checks whether the specified software configuration is
 *        enabled or disabled. 
 *
 *        Usage Example:<br> 
 *        - if(IX_FEATURE_CTRL_SWCONFIG_DISABLED != 
 *            ixFeatureCtrlSwConfigurationCheck(IX_FEATURECTRL_ETH_LEARNING)) <br>
 *        - if(IX_FEATURE_CTRL_SWCONFIG_ENABLED == 
 *            ixFeatureCtrlSwConfigurationCheck(IX_FEATURECTRL_ETH_LEARNING)) <br>
 *
 * This function is typically called during access component initialization time. 
 *
 * @param swConfigType @ref IxFeatureCtrlSwConfig [in] - the type of a software configuration
 *        defined in IxFeatureCtrlSwConfig enumeration.          
 *        
 * @return 
 *      - IX_FEATURE_CTRL_SWCONFIG_ENABLED if software configuration is enabled. 
 *      - IX_FEATURE_CTRL_SWCONFIG_DISABLED if software configuration is disabled.            
 */ 
PUBLIC IX_STATUS
ixFeatureCtrlSwConfigurationCheck (IxFeatureCtrlSwConfig swConfigType);

/**
 * @ingroup IxFeatureCtrlAPI 
 *
 * @fn void ixFeatureCtrlSwConfigurationWrite (IxFeatureCtrlSwConfig swConfigType, BOOL enabled)
 * 
 * @brief This function enable/disable the specified software configuration.  
 *
 *        Usage Example:<br> 
 *        - ixFeatureCtrlSwConfigurationWrite(IX_FEATURECTRL_ETH_LEARNING, TRUE) is used
 *          to enable Ethernet Learning Feature <br>
 *        - ixFeatureCtrlSwConfigurationWrite(IX_FEATURECTRL_ETH_LEARNING, FALSE) is used
 *          to disable Ethernet Learning Feature <br> 
 *
 * @param swConfigType IxFeatureCtrlSwConfig [in] - the type of a software configuration
 *        defined in IxFeatureCtrlSwConfig enumeration. 
 * @param enabled BOOL [in] - To enable(TRUE) / disable (FALSE) the specified software
 *                           configuration.            
 *
 * @return none
 *          
 */ 
PUBLIC void
ixFeatureCtrlSwConfigurationWrite (IxFeatureCtrlSwConfig swConfigType, BOOL enabled);

/**
 * @ingroup IxFeatureCtrlAPI 
 *
 * @fn void ixFeatureCtrlSwVersionShow (void)
 * 
 * @brief This function shows the current software release information for the device 
 *          
 * @return none
 *          
 */ 
PUBLIC void
ixFeatureCtrlSwVersionShow (void);

#include "../featureCtrl/include/IxFeatureCtrl_sp.h"

#endif /* IXFEATURECTRL_H */

/**
 * @} defgroup IxFeatureCtrlAPI
 */
