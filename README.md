# RemoteOps: Remote System Monitoring and Management Tool (IE3090)

## Student & Personalisation Details
* **Student Name:** Sujeewantha K P D S S S (Sasmitha Kariyawasam)
* **Registration Number:** `IT24101660`

| Personalisation Item | Formula | Calculated Value |
| :--- | :--- | :--- |
| **Full Registration Number** | Official SLIIT Reg No | `IT24101660` |
| **Agent Listening Port** | `7000 + 2410` (First 4 digits) | `9410` |
| **Source File Names** | `agent_<last3>.c`, `controller_<last3>.c` | `agent_660.c`, `controller_660.c` |
| **Makefile Name** | `Makefile_<last3>` | `Makefile_660` |
| **Session ID (SID) Tag** | Last 4 digits (`1660`) reversed | `SID:0661` |
| **Authentication Token** | `"OPS-" + 1660` | `OPS-1660` |
| **Log File Name** | `remoteops_<FullRegNo>.log` | `remoteops_IT24101660.log` |
| **File Storage Path** | `./agentfiles/<FullRegNo>/<filename>` | `./agentfiles/IT24101660/<filename>` |
| **Submission Archive** | `IE3090_<FullRegNo>.zip` / `<FullRegNo>.zip` | `IE3090_IT24101660.zip` / `IT24101660.zip` |

## Build and Run Instructions
1. Compile using the personalised Makefile: `make -f Makefile_660`
2. Start the Agent server: `./agent_660`
3. Start the Controller client: `./controller_660 127.0.0.1 9410`
