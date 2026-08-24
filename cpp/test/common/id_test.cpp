/**
 * Test: 
 * type separation, 
 * Nil behavior, 
 * Random ID round-trip, 
 * Malformed hex rejection
*/

#include "cpp/src/dar/common/id.h"

#include <type_traits>

#include <gtest/gtest.h>

namespace dar
{
    namespace
    {
        // static_assert: compile-time assertion mechanism that stops compilation and displays an error message if a condition specified is false
        // std::is_same_v: compile-time helper, check whether two types are exactly identitcal
        // std::is_convertible_v: compile-time helper, check if a type can be implicitly converted to another
        // is they can, the !can -> false, assert display error message
        // We want to check for type separation (StrongID<struct type1> & StrongID<struct type2>)
        static_assert(!std::is_same_v<AgentID,TaskID>);
        static_assert(!std::is_convertible_v<AgentID,TaskID>);

        // TEST macro: primary tool used to write basic, independent unit tests
        // TEST(TestSuiteName, TestName){}
        TEST(IdTest, DefaultConstructedIsNil)
        {
            // AgentID: StrongID<AgentIdTag>
            // StrongID default constructor: bytes_{}: 0 for all bytes
            const AgentID id;
            // EXPECT_TRUE verifies that a given boolean condition evaluate to true
            // Even if check fails, Gtest mark the test as failed but allow rest of teset case to continue running
            EXPECT_TRUE(id.IsNil());
            // check if two is equal, but allow continue running even failed(will mark as failed)
            EXPECT_EQ(id.Hex(),"00000000000000000000000000000000");
        }

        // The function Nil should produce Nil ID (all 0)
        TEST(IdTest, NilFactoryPruduceNilId)
        {
            const AgentID id = AgentID::Nil();

            EXPECT_TRUE(id.IsNil());
            EXPECT_EQ(id, AgentID{});  
        }

        TEST(IdTest, RandomIdIsNotNil)
        {
            const AgentID id = AgentID::Random();
            EXPECT_FALSE(id.IsNil());
        }

        TEST(IdTest, RandomIdRoundTripsThroughHex)
        {
            const AgentID id = AgentID::Random();
            EXPECT_FALSE(id.IsNil());

            const std::string hex = id.Hex();
            // The hex Id generated should have 32 bytes
            EXPECT_EQ(hex.size(),32U);

            const auto parsed = AgentID::FromHex(hex);

            ASSERT_TRUE(parsed.has_value());
            // After going from IdByte id -> hex -> string, it should have the same value
            EXPECT_EQ(*parsed,id);
        }

        TEST(IdTest, ParsedUppercaseHex)
        {
            const auto parsed = AgentID::FromHex("0123456789ABCDEF");

            // If condition is false, it generates a failure and return to the header (drop immediately)
            ASSERT_TRUE(parsed.has_value());

            // When converted back(hex -> IdByte id), it should be all lowercase
            EXPECT_EQ(parsed->Hex(),"0123456789abcdef");
        }

        // ?
        TEST(IdTest, RejectsTooShortHex)
        {
            const auto parsed = AgentID::FromHex("0123456789abcdef");

            // Return a warning if the statement is not false
            EXPECT_FALSE(parsed.has_value());
        }

        TEST(IdTest, RejectsTooLongHex)
        {
            const auto parsed = AgentID::FromHex("0123456789abcdef0123456789abc00");
            EXPECT_FALSE(parsed.has_value());
        }

        TEST(IdTest, RejectsInvalidHexCharacters)
        {
            const auto parsed = AgentID::FromHex("0123456789abcdef0123456789abcdefg");

            EXPECT_FALSE(parsed.has_value());
        }
        
        TEST(IdTest, ParseNilHexAsNilId)
        {
            const auto parsed = AgentID::FromHex("00000000000000000000000000000000");

            ASSERT_TRUE(parsed.has_value());
            EXPECT_TRUE(parsed->IsNil());
            // Dereference a *pointer from FromHex should be the same value as 0 (id.h:private)
            EXPECT_EQ(*parsed,AgentID::Nil());
        }

        // usually, not every time as coincidence occur
        TEST(IdTest, DifferentRandomIDsAreUsuallyDifferent)
        {
            const AgentID first = AgentID::Random();
            const AgentID second = AgentID::Random();

            // Verifies the two value are not equal (allow continue even if failure)
            EXPECT_NE(first,second);
        }
    }
}
