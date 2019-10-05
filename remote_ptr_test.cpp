

#include <vector>
#include <cstdint>
#include <cstring>
#include <iostream>

std::vector<std::uint8_t> g_buffer;

void read_remote(std::uintptr_t address, std::uint8_t* buffer, std::size_t n) {
    std::memcpy(buffer, &g_buffer[address], n);
}

void write_remote(std::uintptr_t address, std::uint8_t* buffer, std::size_t n) {
    std::memcpy(&g_buffer[address], buffer, n);
}

using read_buffer_func = std::add_pointer<void(std::uintptr_t address, std::uint8_t* buffer, std::size_t n)>::type;

using write_buffer_func = std::add_pointer<void(std::uintptr_t address, std::uint8_t* buffer, std::size_t n)>::type;

/**
 * Abstract continguous memory accesses to variables
 */
template <typename T, read_buffer_func read_remote, write_buffer_func write_remote>
class remote_ptr {
public:
    using value_type = T;
    using instance_type = remote_ptr<value_type, read_remote, write_remote>;

    explicit remote_ptr(std::uintptr_t address) :
        m_address(address) {
        
    };

    // A class meant to be returned as a temporary
    // Reads on construction, writes on destruction
    class value_proxy {
    public:
        value_proxy(instance_type& exterior) :
            m_exterior(exterior) {
            std::cout << "value_proxy()" << std::endl;

            m_initial_value_buffer.resize(sizeof(value_type));
            read_remote(m_exterior.m_address, &m_initial_value_buffer[0], m_initial_value_buffer.size());
            for(int i = 0; i < m_initial_value_buffer.size()/8; i++) {
                std::cout << "Read: " << ((int*)(m_initial_value_buffer.data()))[i] << std::endl;
            }

            m_value_buffer = m_initial_value_buffer;


            //std::cout << "Initial read " << *this << std::endl;
        };

        ~value_proxy() {
            std::cout << "~value_proxy()" << std::endl;
            // Walk the two buffers we started and finished with and compare changes before and after creation of this class
            std::size_t diff_count = 0;

            auto original_it = m_initial_value_buffer.begin();
            auto changed_it = m_value_buffer.begin();
            while(original_it != m_initial_value_buffer.end()) {
                auto offset = std::distance(m_initial_value_buffer.begin(), original_it);

                // If this byte does not match increment the sequence of changed bytes
                if(*original_it != *changed_it) {
                    diff_count++;
                }
                else if (diff_count > 0) { // If the byte does match and there was a previous sequence being tracked
                    // The sequence has ended
                    // Commit all previous changes
                    write_remote(m_exterior.m_address + offset - diff_count, &m_value_buffer[offset - diff_count], diff_count);
                    std::cout << "Committing " << diff_count << " bytes to " << m_exterior.m_address + offset - diff_count << std::endl;

                    diff_count = 0;
                }

                ++changed_it;
                ++original_it;
            }
        };

    private:
        // Raw data buffers
        std::vector<std::uint8_t> m_initial_value_buffer;
        std::vector<std::uint8_t> m_value_buffer;
        instance_type& m_exterior;

        constexpr value_type* value() {
            std::cout << "value()" << std::endl;
            for(int i = 0; i < m_initial_value_buffer.size()/8; i++) {
                std::cout << "Read: " << ((int*)(m_value_buffer.data()))[i] << std::endl;
            }
            return reinterpret_cast<value_type*>(m_value_buffer.data());
        }

    public:
        operator value_type&() {
            std::cout << "value_type()" << std::endl;
            return reinterpret_cast<value_type&>(*value());
        }

        operator const value_type&() const {
            return reinterpret_cast<const value_type&>(*value());
        }

        typedef typename std::remove_pointer<value_type>::type value_type_deref;
        /**
         * Dereference the value remotely if T is a pointer type
         * @notes This reads the value stored at the memory address, and sets the address to it
         *        Disabled for non-pointer types
         * @return A value represented the dereferenced remote value
         */
        template <typename U = value_type, std::enable_if_t<std::is_pointer<U>::value, int> = 0>
        remote_ptr<value_type_deref, read_remote, write_remote>::value_proxy operator*() {
            return *(remote_ptr<value_type_deref, read_remote, write_remote>(reinterpret_cast<std::uintptr_t>(*value())));
        }
    };

    class class_proxy {
    public:
        class_proxy(instance_type& exterior) :
            m_proxy(exterior) {
        };

        value_type* operator->() {
            return &((value_type&)m_proxy);
        };

    private:
        value_proxy m_proxy;
    };

    template <typename U = value_type, std::enable_if_t<std::is_class<U>::value, int> = 0>
    class_proxy operator->() {
        return class_proxy(*this);
    }

    template <typename U = value_type, std::enable_if_t<std::is_class<U>::value, int> = 0>
    value_type& operator*() {
        return value_proxy(*this);
    }

    template <typename U = value_type, std::enable_if_t<!std::is_class<U>::value, int> = 0>
    value_proxy operator*() {
        return value_proxy(*this);
    }

private:
    std::uintptr_t m_address;
};

template <typename value_type>
class test_object {
    public:
        test_object() {
            m_val = 0;
        }

        explicit test_object(value_type initial) 
            : m_val(initial) {

        };

        template <typename ArgR>
        test_object& operator+=(ArgR&& other) {
            m_val += other;
            return *this;
        };

        template <typename ArgR>
        test_object& operator/=(ArgR&& other) {
            m_val /= other;
            return *this;
        };

        template <typename T>
        friend std::ostream& operator<<(std::ostream& os, const test_object obj);

        const value_type& get() const {
            return m_val;
        };

        bool operator!() const {
            return false;
        }

        value_type operator--(int) {
            auto preserve = m_val;
            m_val--;
            return preserve;
        }

        value_type operator--() {
            return --m_val;
        }

    //private:
        value_type m_val;

};

struct test {
    int a, b, c, d;
};

int main(int argc, char** argv) {
    //using a = test_object<std::uint32_t>;
    g_buffer.resize(32);
    // test a {0xFFFFFFF, 0xCCCCCC, 0xEFEFEFE, 0xDEDEDE};
    // test b;
    // write_remote(0,(std::uint8_t*)&a,sizeof(test));
    // read_remote(0, (std::uint8_t*)&b, sizeof(test));
    // std::cout << a.a << " " << a.b << " " << a.c << " " << a.d << " " << std::endl;
    // std::cout << b.a << " " << b.b << " " << b.c << " " << b.d << " " << std::endl;
    // remote_ptr<test, read_remote, write_remote> ab(0);
    // ab->a = 0;
    // ab->b = 7;

    // read_remote(0, (std::uint8_t*)&a, sizeof(test));

    // std::cout << a.a << " " << a.b << " " << a.c << " " << a.d << " " << std::endl;
    // // std::cout << "values:" << std::endl;
    // // std::cout << "- " << ab->a << std::endl;
    // // std::cout << "- " << ab->b << std::endl;
    // // std::cout << "- " << ab->c << std::endl;
    // // std::cout << "- " << ab->d << std::endl;
    *(std::uintptr_t*)&g_buffer[0] = 0x0008;
    *(std::uintptr_t*)&g_buffer[0x8] = 0xDEADBEEF;
    remote_ptr<std::uintptr_t*, read_remote, write_remote> ab(0);
    remote_ptr<std::uintptr_t, read_remote, write_remote> c(0);

    std::cout << *(*ab) << std::endl;

    return 0;
}

// readprocessmemory(&buf, 0x400, 4);
// buf[2] = 5
// writeprocessmemory(&buf, 0x400, 4);