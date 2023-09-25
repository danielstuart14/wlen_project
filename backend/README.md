# WLEN backend, based on: Azure Functions custom handler in Rust

This should be deployed as an Azure function [custom handler](https://docs.microsoft.com/azure/azure-functions/functions-custom-handlers)

## Pre-reqs

- Rust : https://www.rust-lang.org/learn/get-started
- Azure Functions Core Tools
- Define the Cosmos DB URI in the Cargo.toml

The host.json expects the server binary to be at the root of this directory.