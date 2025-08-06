#include "utils/fast_string_conversion.h"
#include <cstring>
#include <algorithm>

namespace fix_gateway::utils
{
    // Thread-local buffer definitions
    thread_local char FastStringConversion::int_buffer_[INT_BUFFER_SIZE];
    thread_local char FastStringConversion::double_buffer_[DOUBLE_BUFFER_SIZE];
    
    std::string_view FastStringConversion::int_to_string(int value)
    {
        return integer_to_string_internal(value, int_buffer_, INT_BUFFER_SIZE);
    }
    
    std::string_view FastStringConversion::long_to_string(long value)
    {
        return integer_to_string_internal(value, int_buffer_, INT_BUFFER_SIZE);
    }
    
    std::string_view FastStringConversion::uint_to_string(unsigned int value)
    {
        return integer_to_string_internal(value, int_buffer_, INT_BUFFER_SIZE);
    }
    
    template<typename T>
    std::string_view FastStringConversion::integer_to_string_internal(T value, char* buffer, size_t buffer_size)
    {
        char* end = buffer + buffer_size - 1; // Leave space for null terminator
        char* start = end;
        
        // Handle zero case
        if (value == 0) {
            *--start = '0';
            return std::string_view(start, 1);
        }
        
        // Handle negative numbers
        bool negative = false;
        if constexpr (std::is_signed_v<T>) {
            if (value < 0) {
                negative = true;
                // Handle overflow case for most negative number
                if (value == std::numeric_limits<T>::min()) {
                    // Use snprintf for edge case to avoid overflow
                    int len = snprintf(buffer, buffer_size, "%lld", static_cast<long long>(value));
                    return std::string_view(buffer, len);
                }
                value = -value;
            }
        }
        
        // Extract digits (right to left)
        while (value > 0) {
            *--start = '0' + (value % 10);
            value /= 10;
        }
        
        // Add negative sign if needed
        if (negative) {
            *--start = '-';
        }
        
        return std::string_view(start, end - start);
    }
    
    std::string_view FastStringConversion::double_to_string(double value, int precision)
    {
        // Handle special cases
        if (std::isnan(value)) {
            return std::string_view("NaN", 3);
        }
        if (std::isinf(value)) {
            return value < 0 ? std::string_view("-inf", 4) : std::string_view("inf", 3);
        }
        
        // Use snprintf for consistent, fast formatting
        // Precision is clamped to reasonable range for FIX protocol
        precision = std::max(0, std::min(precision, 9));
        
        int len = snprintf(double_buffer_, DOUBLE_BUFFER_SIZE, "%.*f", precision, value);
        
        // Handle error case
        if (len < 0 || len >= static_cast<int>(DOUBLE_BUFFER_SIZE)) {
            return std::string_view("0.00", 4); // Safe fallback
        }
        
        return std::string_view(double_buffer_, len);
    }
    
    std::string_view FastStringConversion::double_to_string_auto(double value)
    {
        // Handle special cases
        if (std::isnan(value)) {
            return std::string_view("NaN", 3);
        }
        if (std::isinf(value)) {
            return value < 0 ? std::string_view("-inf", 4) : std::string_view("inf", 3);
        }
        
        // Determine optimal precision based on value magnitude
        double abs_value = std::abs(value);
        int precision;
        
        if (abs_value >= 1000000.0) {
            precision = 0; // Large numbers: no decimal places
        } else if (abs_value >= 1000.0) {
            precision = 2; // Medium numbers: 2 decimal places
        } else if (abs_value >= 1.0) {
            precision = 4; // Small numbers: 4 decimal places
        } else {
            precision = 6; // Very small numbers: 6 decimal places
        }
        
        return double_to_string(value, precision);
    }
    
    std::string FastStringConversion::make_permanent(std::string_view view)
    {
        return std::string(view);
    }
    
    // Explicit template instantiations for common types
    template std::string_view FastStringConversion::integer_to_string_internal<int>(int, char*, size_t);
    template std::string_view FastStringConversion::integer_to_string_internal<long>(long, char*, size_t);
    template std::string_view FastStringConversion::integer_to_string_internal<unsigned int>(unsigned int, char*, size_t);
}