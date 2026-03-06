import { NextRequest, NextResponse } from 'next/server';
import zmq from 'zeromq';
import { v4 as uuidv4 } from 'uuid';

/**
 * This route acts as a bridge given zmq is a server-side only library.
 */

let dealer: zmq.Dealer | null = null;
let isConnecting = false;
const clientUUID = uuidv4();
const pendingRequests = new Map<
  string,
  {
    resolve: (value: any) => void;
    reject: (reason?: any) => void;
    timeout: NodeJS.Timeout;
  }
>();

async function initializeDealer() {
  if (dealer) return;
  if (isConnecting) {
    // Wait for connection to complete
    return new Promise((resolve) => {
      const checkInterval = setInterval(() => {
        if (dealer) {
          clearInterval(checkInterval);
          resolve(null);
        }
      }, 100);
    });
  }

  isConnecting = true;
  try {
    dealer = new zmq.Dealer();
    
    // Set a readable client ID
    const clientId = 'shopping-list-app' + '-' + clientUUID;
    (dealer as any).routingId = clientId;
    
    const routerEndpoint = 'tcp://localhost:5555';

    dealer.connect(routerEndpoint);
    console.log(`[API Route] Connected to router at ${routerEndpoint} with ID: ${clientId}`);

    receiveResponses();
  } catch (error) {
    console.error('[API Route] Failed to initialize Dealer socket:', error);
    dealer = null;
    throw error;
  } finally {
    isConnecting = false;
  }
}

// Continuously receive responses from router
async function receiveResponses() {
  if (!dealer) return;

  try {
    for await (const [envelope, messageData] of dealer) {
      try {
        const messageStr = messageData.toString();
        const response = JSON.parse(messageStr);
        const requestId = response.request_id;

        if (requestId && pendingRequests.has(requestId)) {
          const pending = pendingRequests.get(requestId)!;
          clearTimeout(pending.timeout);
          pending.resolve(response);
          pendingRequests.delete(requestId);
        } else {
          console.warn(
            '[API Route] Received response for unknown request_id:',
            requestId
          );
        }
      } catch (error) {
        console.error('[API Route] Failed to parse router response:', error);
      }
    }
  } catch (error) {
    console.error('[API Route] Error receiving responses:', error);
  }
}

// Send message to router and wait for response
async function sendToRouter(
  type: string,
  listName: string,
  payload: any,
  timeoutMs: number = 5000
): Promise<any> {
  await initializeDealer();

  if (!dealer) {
    throw new Error('Failed to initialize ZMQ Dealer socket');
  }

  const requestId = uuidv4();
  const senderEndpoint = 'tcp://shopping-list-app';

  // Create message in Protocol format
  const message = {
    type,
    sender_endpoint: senderEndpoint,
    request_id: requestId,
    payload: {
      list_name: listName,
      ...payload,
    },
  };

  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      pendingRequests.delete(requestId);
      reject(new Error(`Request ${requestId} timed out after ${timeoutMs}ms`));
    }, timeoutMs);

    pendingRequests.set(requestId, { resolve, reject, timeout });

    try {
      const messageStr = JSON.stringify(message);
      dealer!.send([Buffer.from(messageStr)]).catch((err: any) => {
        clearTimeout(timeout);
        pendingRequests.delete(requestId);
        reject(err);
      });
    } catch (error) {
      clearTimeout(timeout);
      pendingRequests.delete(requestId);
      reject(error);
    }
  });
}

export async function POST(request: NextRequest) {
  try {
    const body = await request.json();
    const { type, payload, list_name } = body;

    if (!type || !list_name) {
      return NextResponse.json(
        { error: 'Missing required fields: type, list_name' },
        { status: 400 }
      );
    }

    console.log(`[API Route] Received ${type} request for list: ${list_name}`);

    const response = await sendToRouter(type, list_name, payload || {});

    return NextResponse.json(response);
  } catch (error: any) {
    const message = error?.message || 'Internal server error';
    if (message.includes('timed out')) {
      // Router not reachable within timeout — treat as expected offline case, respond with 504
      return NextResponse.json({ error: 'Router timeout' }, { status: 504 });
    }
    if (message.includes('Failed to initialize ZMQ Dealer socket')) {
      return NextResponse.json({ error: message }, { status: 503 });
    }

    console.error('[API Route] Error handling request:', error);
    return NextResponse.json({ error: message }, { status: 500 });
  }
}

