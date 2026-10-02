# Stage 3 - Architecture, UML and Design

All diagrams (system architecture, component/thread architecture, network architecture, flowchart, data-flow diagram, use-case,
sequence diagrams, class diagram, module descriptions) are in:

- [`../architecture.md`](../architecture.md) - system, thread/process and network architecture, request and health-check data flow, shutdown order
- [`../design.md`](../design.md) - module table, class diagram, use-case diagram, sequence diagrams (normal and failure/recovery), flowchart, DFD, design decisions

The diagrams are drawn in text so they stay in Git and match the real classes: `Router` / `RoundRobinRouter` / `LeastConnectionsRouter`, `BackendPool`,
`HealthChecker`, `ThreadPool`, `Statistics`, plus the free-function modules `logger`, `signal_handler` and `net_utils`.
If your teacher wants graphical UML, redraw these in draw.io/PlantUML/Dia using the same names (the structure is already correct).
