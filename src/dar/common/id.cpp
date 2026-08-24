/**
 * Centralize entropy generation and hex encoding,
 * Centralizing generation behind: detail::GenerateRandomIDBytes() (will be replaced later)
 * Compile-time test (Make illegal combinations impossible to express):
 * static_assert(!std::is_same_v<AgentID, TaskID>);
 * static_assert(!std::is_convertible_v<AgentID,TaskID>);
 */

// std::nullopt is a constant used to represent an empty or uninitialized state for std::optional object


#include "dar/common/id.h"
#include <random>

namespace dar::detail
{
    // This namespace is private to this file (id.cpp)
    namespace
    {
        // This is a lookup table for converting numerical value from 0-15 to hexidecimal
        // Ex: kHexDigit[10] = 'a'
        // constexpr: This is constant data whose value is known at compile time
        constexpr char kHexDigits[] = "0123456789abcdef";
        [[nodiscard]] constexpr int HexDigitValue(char c) noexcept
        {
            // return what digit this is if its within the range
            if(c>='0' && c<='9')
            {
                return c-'0';
            }
            if(c>='a'&&c<='f')
            {
                return c-'a'+10;
            }
            if(c>='A'&&c<='F')
            {
                return c-'A'+10;
            }
            return -1;
        }
    }

    IdBytes GenerateRandomIDBytes()
    {
        // Create a c++ standard random number source
        std::random_device random_device;
        // Direct value-initiailization of a variable(0 or default state)
        // using IdBytes=std::array<std::uint8_t,16>;
        // {} perform value initialization (16 bytes, all to 0)
        //IdBytes bytes -> underlying integer elements not initialized to meaningful defined values
        IdBytes bytes{};

        // the reference & alter the value
        // Call randomNum once per byte, move from 0 to 15 (16 bytes)
        for(auto& byte:bytes)
        {
            // random_device() ask for a random number
    
            byte = static_cast<std::uint8_t>(random_device());
        }
        return bytes;
    }

    // ID is exactly 16 bytes (16 byte * 2hex char/byte = 32 char)
    std::optional<IdBytes> ParseHexID(std::string_view hex) noexcept
    {
        // Use {} to create a temporary IdBytes object, value-initialized
        // hex must be 32 in length (16 bytes * 2 hex/bytes = 32 hex)
        // 1 hex = 4 bit (2^4=16, hex:16) 1 byte = 8 bits
        if(hex.size() != IdBytes{}.size()*2)
        {
            // If the hex isn't exactly 32 characters, it cannot represent one of our 16 byte ID
            return std::nullopt;
        }

        IdBytes bytes{};
        // If user give "a73f..." contains 'a','7','3','f'
        // Our ID needs 0XA7 0X3F ...
        // parser processes two characters at a time
        for(std::size_t i =0;i<bytes.size();++i)
        {
            // Find each index's corresponding hexDigit to digit (decimal)
            // (i.i*2)0. 0, 1. 2, 2.4
            const int high = HexDigitValue(hex[i*2]);
            // (i.i*2+1)0. 1, 1. 3, 2. 5
            const int low = HexDigitValue(hex[i*2+1]);
            if(high<0||low<0)
            {
                return std::nullopt;
            }

            // Ex: 'a'->10,'7'->7
            /**
             * 10->1010, 7->0111. 10100111->0XA7
             */
            // bytes[0] -> 0XA7 (1byte=8bits=2hex)
            bytes[i]=static_cast<std::uint8_t>(static_cast<unsigned int>(high)<< 4U) | static_cast<unsigned int>(low);
        }
        return bytes;
    }

    // Now binary ID contains: A7 3F 91 ....
    // HexID produce a73f91... (Exact opposite of ParseHexId())
    // bytes hold a 16-bytes ID
    std::string HexID(const IdBytes& bytes)
    {
        std::string hex;
        // each byte, 2 hex number
        hex.resize(bytes.size()*2);
        for(std::size_t i =0; i<bytes.size();++i)
        {
            const auto byte = bytes[i];
            // 0: byte = 0XA7 (Binary: high(1010->A), low(0111->7))
            // 0: i*2 = 0. 
            // byte >> 4U (shift right by four bits -> 00001010)
            // & 0X0FU (keep only lowest four bits) -> 1010 (10)
            //kHexDigit[10]->'a' (put on index 0)
            hex[i*2] = kHexDigits[(byte>>4U)&0X0FU];
            hex[i*2+1]=kHexDigits[byte & 0X0FU];
        }
        return hex;
    }
}