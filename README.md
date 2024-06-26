# Psuedo-TCP - End-to-End Reliable Data Transfer

## Introduction

Psudeo-TCP is a custom-built protocol designed to ensure reliable end-to-end data transfer over unreliable communication channels. This project involves implementing MTP(My Transfer Protocol) sockets, ensuring messages are reliably delivered in order, similar to UDP sockets but with additional reliability mechanisms.

This project was developed by Raj Parikh (myself) and Soukhin Nayek as part of the Computer Networks Laboratory course.
## Features

- **Reliable Data Transfer**: Messages are reliably delivered to the receiver in order, even over unreliable communication channels.
- **Window-Based Flow Control**: MTP implements window-based flow control to optimize data transfer and prevent congestion.
- **Out-of-Order Handling**: MTP handles out-of-order messages and ensures that they are delivered in sequence.
- **Retransmission**: Unacknowledged messages are retransmitted to ensure reliable delivery.
- **Simulated Unreliable Link**: The project includes functionality to simulate an unreliable link for testing purposes.

## Getting Started

To use MTP in your project, follow these steps:

1. Clone the repository: `git clone https://github.com/your-username/mtp.git`
2. Include the MTP library in your project.
3. Use the provided functions (`m_socket`, `m_bind`, `m_sendto`, `m_recvfrom`, `m_close`) to work with MTP sockets.
4. Compile and link your project with the MTP library.

## Testing

To test the reliability of MTP, run the provided test programs:

1. `user1.c`: Creates an MTP socket and sends a large file.
2. `user2.c`: Receives the file using an MTP socket.

You can vary the probability parameter `p` to observe the average number of transmissions required to send each message under different conditions.

## Contributing

Contributions are welcome! If you'd like to contribute to the development of MTP, please follow these steps:

1. Fork the repository.
2. Create a new branch: `git checkout -b feature/my-feature`.
3. Make your changes and commit them: `git commit -m 'Add new feature'`.
4. Push to the branch: `git push origin feature/my-feature`.
5. Submit a pull request.


## Acknowledgements

This project was inspired by the need for reliable data transfer over unreliable communication channels. Special thanks to the contributors who have helped improve and maintain MTP.

---
**Note**: Detailed instructions for implementation and testing are provided in the project documentation.
