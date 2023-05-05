import gdb

class printWorkQueue(gdb.Command):

    def __init__(self):
        super(printWorkQueue, self).__init__("printWorkQueue", gdb.COMMAND_USER)
        self.doc = "print a work queue"
        self.dont_repeat()

    def invoke(self, arg, from_tty):
        args = gdb.string_to_argv(arg)
        if len(args) != 1:
            print("Usage: printWorkQueue <queue-index>")
            return
        try:
            queue_num = gdb.parse_and_eval(args[0])
        except gdb.error:
            print("Invalid address: {}".format(args[0]))
            return

        head = gdb.parse_and_eval(f"runtime.workers[{queue_num}].head")

        # Traverse the linked list
        node = head["next"]
        print(f"Q{queue_num}: ", end='')

        while node != head:
            # Print the address and value of each node
            print(f"({node['id']}-{node['stack']}), ", end='')
            node = node["next"]
        
        print('')

# Register the command with GDB
printWorkQueue()
